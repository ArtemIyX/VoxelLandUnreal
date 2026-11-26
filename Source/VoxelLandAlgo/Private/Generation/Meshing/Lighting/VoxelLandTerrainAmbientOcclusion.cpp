#include "Generation/Meshing/Lighting/VoxelLandTerrainAmbientOcclusion.h"

#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TRACE_DECLARE_INT_COUNTER(
	VoxelTerrainAmbientVisibilitySamples, TEXT("VoxelLand/Terrain Ambient Visibility Completed Lattice Reads"));

class FVoxelLandAmbientOcclusionKernel final
{
public:
	using FSampler = TFunctionRef<FVoxelLandError(const FInt64Vector&, int16&)>;

	explicit FVoxelLandAmbientOcclusionKernel(FSampler InSample)
		: Sample(InSample)
	{
	}

	static constexpr int32 MaxVertices = 128 * 1024;
	static constexpr int64 AnchorUnitsPerVoxel = 1 << 16;
	static constexpr double MaxBaseCoordinate = 1 << 28;
	static constexpr double DensityUnitsPerVoxel = 1024.0;
	static constexpr int32 CancelInterval = 32;
	static constexpr int32 DirectionsPerRing = 4;
	static constexpr int32 LatticeReadsPerAnchor = 8 * (1 + 2 * DirectionsPerRing);

	static FVoxelLandError Cancelled()
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Terrain ambient visibility cancelled")};
	}

	[[nodiscard]] FVoxelLandError Evaluate(const FInt64Vector& Anchor, uint8& OutVisibility) const;

private:
	static constexpr double NearRadius = 2.0;
	static constexpr double FarRadius = 4.0;
	static constexpr double NearLift = 0.625;
	static constexpr double FarLift = 1.25;
	static constexpr double NearWeight = 2.0;
	static constexpr double FarWeight = 1.0;

	[[nodiscard]] FVoxelLandError ReadCell(
		const FVector3d& Point, double (&OutCorners)[2][2][2], FVector3d& OutFraction) const;
	[[nodiscard]] FVoxelLandError ReadRing(const FVector3d& Center, const FVector3d& Tangent,
		const FVector3d& Bitangent, double Radius, double& OutOcclusion) const;
	static FVector3d Gradient(const double (&Corners)[2][2][2], const FVector3d& Fraction);
	static double Interpolate(const double (&Corners)[2][2][2], const FVector3d& Fraction);

	FSampler Sample;
};

FVoxelLandError FVoxelLandAmbientOcclusionKernel::ReadCell(
	const FVector3d& Point, double (&OutCorners)[2][2][2], FVector3d& OutFraction) const
{
	const FInt64Vector Cell(FMath::FloorToInt64(Point.X), FMath::FloorToInt64(Point.Y), FMath::FloorToInt64(Point.Z));
	OutFraction = Point - FVector3d(Cell);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const int32 X = Corner & 1;
		const int32 Y = (Corner >> 1) & 1;
		const int32 Z = Corner >> 2;
		int16 Density = 0;
		if (const FVoxelLandError Error = Sample(Cell + FInt64Vector(X, Y, Z), Density))
		{
			return Error;
		}
		OutCorners[Z][Y][X] = Density;
	}
	return {};
}

FVector3d FVoxelLandAmbientOcclusionKernel::Gradient(const double (&Corners)[2][2][2], const FVector3d& Fraction)
{
	const double DX0 =
		FMath::Lerp(Corners[0][0][1] - Corners[0][0][0], Corners[0][1][1] - Corners[0][1][0], Fraction.Y);
	const double DX1 =
		FMath::Lerp(Corners[1][0][1] - Corners[1][0][0], Corners[1][1][1] - Corners[1][1][0], Fraction.Y);
	const double DY0 =
		FMath::Lerp(Corners[0][1][0] - Corners[0][0][0], Corners[0][1][1] - Corners[0][0][1], Fraction.X);
	const double DY1 =
		FMath::Lerp(Corners[1][1][0] - Corners[1][0][0], Corners[1][1][1] - Corners[1][0][1], Fraction.X);
	const double DZ0 =
		FMath::Lerp(Corners[1][0][0] - Corners[0][0][0], Corners[1][0][1] - Corners[0][0][1], Fraction.X);
	const double DZ1 =
		FMath::Lerp(Corners[1][1][0] - Corners[0][1][0], Corners[1][1][1] - Corners[0][1][1], Fraction.X);
	return FVector3d(
		FMath::Lerp(DX0, DX1, Fraction.Z), FMath::Lerp(DY0, DY1, Fraction.Z), FMath::Lerp(DZ0, DZ1, Fraction.Y));
}

double FVoxelLandAmbientOcclusionKernel::Interpolate(const double (&Corners)[2][2][2], const FVector3d& Fraction)
{
	const double Bottom = FMath::Lerp(FMath::Lerp(Corners[0][0][0], Corners[0][0][1], Fraction.X),
		FMath::Lerp(Corners[0][1][0], Corners[0][1][1], Fraction.X), Fraction.Y);
	const double Top = FMath::Lerp(FMath::Lerp(Corners[1][0][0], Corners[1][0][1], Fraction.X),
		FMath::Lerp(Corners[1][1][0], Corners[1][1][1], Fraction.X), Fraction.Y);
	return FMath::Lerp(Bottom, Top, Fraction.Z);
}

FVoxelLandError FVoxelLandAmbientOcclusionKernel::ReadRing(const FVector3d& Center, const FVector3d& Tangent,
	const FVector3d& Bitangent, double Radius, double& OutOcclusion) const
{
	const FVector3d Offsets[DirectionsPerRing] = {
		Tangent * Radius, Bitangent * Radius, -Tangent * Radius, -Bitangent * Radius};
	OutOcclusion = 0.0;
	for (const FVector3d& Offset : Offsets)
	{
		double Corners[2][2][2];
		FVector3d Fraction;
		if (const FVoxelLandError Error = ReadCell(Center + Offset, Corners, Fraction))
		{
			return Error;
		}
		const double Density = Interpolate(Corners, Fraction);
		OutOcclusion += FMath::Clamp(-Density / DensityUnitsPerVoxel, 0.0, 1.0);
	}
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionKernel::Evaluate(const FInt64Vector& Anchor, uint8& OutVisibility) const
{
	double Corners[2][2][2];
	FVector3d Fraction;
	const FVector3d Point = FVector3d(Anchor) / double(AnchorUnitsPerVoxel);
	if (const FVoxelLandError Error = ReadCell(Point, Corners, Fraction))
	{
		return Error;
	}

	// The completed-field gradient is independent of tessellation and vertex normal averaging.
	const FVector3d Normal = Gradient(Corners, Fraction).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UpVector);
	const FVector3d Axis = FMath::Abs(Normal.Z) < 0.9 ? FVector3d::UpVector : FVector3d::ForwardVector;
	const FVector3d Tangent = FVector3d::CrossProduct(Normal, Axis).GetSafeNormal();
	const FVector3d Bitangent = FVector3d::CrossProduct(Normal, Tangent);
	double NearOcclusion = 0.0;
	double FarOcclusion = 0.0;
	if (const FVoxelLandError Error =
			ReadRing(Point + Normal * NearLift, Tangent, Bitangent, NearRadius, NearOcclusion))
	{
		return Error;
	}
	if (const FVoxelLandError Error = ReadRing(Point + Normal * FarLift, Tangent, Bitangent, FarRadius, FarOcclusion))
	{
		return Error;
	}

	constexpr double TotalWeight = DirectionsPerRing * (NearWeight + FarWeight);
	const double Visibility = 1.0 - (NearWeight * NearOcclusion + FarWeight * FarOcclusion) / TotalWeight;
	OutVisibility = static_cast<uint8>(
		FMath::RoundToInt32(FMath::Clamp(Visibility, 0.0, 1.0) * FVoxelLandTerrainAmbientOcclusion::FullyVisible));
	return {};
}

FVoxelLandError FVoxelLandTerrainAmbientOcclusion::Bake(FVoxelLandMeshData& Mesh, float BaseVoxelSizeCm,
	const FVector3d& LogicalOrigin, TFunctionRef<FVoxelLandError(const FInt64Vector&, int16&)> SampleCompletedLattice,
	TFunctionRef<bool()> IsCancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_TerrainAmbientVisibility);
	using FKernel = FVoxelLandAmbientOcclusionKernel;
	if (IsCancelled())
	{
		return FKernel::Cancelled();
	}
	if (Mesh.Vertices.Num() > FKernel::MaxVertices || !FMath::IsFinite(BaseVoxelSizeCm) || BaseVoxelSizeCm <= 0.f)
	{
		return {EVoxelLandErrorCode::InvalidParameter,
			TEXT("Terrain ambient visibility exceeds the mesh budget or has an invalid scale")};
	}
	FKernel Kernel(SampleCompletedLattice);
	TMap<FInt64Vector, uint8> VisibilityByAnchor;
	VisibilityByAnchor.Reserve(Mesh.Vertices.Num());
	TArray<uint8> Visibility;
	Visibility.Reserve(Mesh.Vertices.Num());
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		if (Index % FKernel::CancelInterval == 0 && IsCancelled())
		{
			return FKernel::Cancelled();
		}
		const FVector3d Point = (LogicalOrigin + FVector3d(Mesh.Vertices[Index].Position)) / double(BaseVoxelSizeCm);
		if (Point.ContainsNaN() || Point.GetAbsMax() > FKernel::MaxBaseCoordinate)
		{
			return {EVoxelLandErrorCode::NonFiniteCoordinate,
				TEXT("Terrain ambient visibility anchor is outside the base lattice")};
		}
		const FVector3d FixedPoint = Point * double(FKernel::AnchorUnitsPerVoxel);
		const FInt64Vector Anchor(
			FMath::RoundToInt64(FixedPoint.X), FMath::RoundToInt64(FixedPoint.Y), FMath::RoundToInt64(FixedPoint.Z));
		if (const uint8* Cached = VisibilityByAnchor.Find(Anchor))
		{
			Visibility.Add(*Cached);
			continue;
		}
		uint8 Value = 0;
		if (const FVoxelLandError Error = Kernel.Evaluate(Anchor, Value))
		{
			return Error;
		}
		VisibilityByAnchor.Add(Anchor, Value);
		Visibility.Add(Value);
	}
	if (IsCancelled())
	{
		return FKernel::Cancelled();
	}
	TRACE_COUNTER_ADD(VoxelTerrainAmbientVisibilitySamples, VisibilityByAnchor.Num() * FKernel::LatticeReadsPerAnchor);
	Mesh.AmbientVisibility = MoveTemp(Visibility);
	return {};
}
