#include "Generation/VoxelLandM5SingleChunkRoute.h"

#include "Data/Enums/VoxelLandMeshResult.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5FixedMesh.h"
#include "Generation/VoxelLandM5FixedSupport.h"
#include "Generation/VoxelLandM5MeshAdapter.h"
#include "Generation/VoxelLandM5SweptTriangle.h"
#include "Generation/VoxelLandMeshing.h"

class FVoxelLandM5SingleChunkRouteInternals final
{
public:
	static FVoxelLandM5Diagnostic SingleChunkRouteError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.SingleChunkRoute");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::SingleChunkRoute::Certify(const FVoxelLandChunkData& Chunk, int32 VoxelSizeCm,
	bool bUnlimitedM4, const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm,
	int64 HeightCm, int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<int32> SupportIndices, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
			E::Cancelled, TEXT("Route certification cancelled"));
	}
	constexpr int64 Limit = 1048576;
	if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != FVoxelLandChunkData::SampleCount || VoxelSizeCm < 1
		|| VoxelSizeCm > 134217 || RadiusCm <= 0 || RadiusCm > Limit || HeightCm < 2 * RadiusCm || HeightCm > Limit
		|| MarginCm < 0 || MarginCm > Limit || ContactSkinCm < 0 || ContactSkinCm > Limit || SupportIndices.IsEmpty()
		|| SupportIndices.Num() > 256 || MaxSlopeNumerator < 0 || MaxSlopeDenominator <= 0
		|| !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(Chunk.Key.Coordinate, float(VoxelSizeCm), bUnlimitedM4))
	{
		return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
			E::InvalidInput, TEXT("Invalid single-chunk route inputs"));
	}
	const int64 Radius = RadiusCm + MarginCm; // Inputs are bounded by 2^20.
	const int64 Scale = VoxelSizeCm;
	const FVoxelLandM5PointCm Points[] = {StartFeet, EndFeet};
	for (const auto& Point : Points)
	{
		if (Point.X < -Limit || Point.X > Limit || Point.Y < -Limit || Point.Y > Limit || Point.Z < -Limit
			|| Point.Z > Limit)
		{
			return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
				E::OutOfDomain, TEXT("Route point outside logical domain"));
		}
		const int64 Values[] = {Point.X, Point.Y, Point.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// Chunk coordinates are precision-range checked above and Scale <= 134217.
			const int64 Origin = int64(Chunk.Key.Coordinate[Axis]) * 32 * Scale;
			const int64 Lower = Values[Axis] - (Axis == 2 ? MarginCm : Radius);
			const int64 Upper = Values[Axis] + (Axis == 2 ? HeightCm + MarginCm : Radius);
			if (Lower <= Origin + 2 * Scale || Upper >= Origin + 30 * Scale)
			{
				return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
					E::Unproven, TEXT("Sweep requires neighboring chunk ownership"));
			}
		}
	}
	constexpr int32 MaxVertices = 34 * 34 * 34;
	constexpr int32 MaxTriangles = 32 * 32 * 32 * 3 * 2;
	// Reserve before running the existing mesher or adapter. Both can grow UE
	// arrays; charge four times their maximum element payload plus bookkeeping.
	const int64 Bytes =
		4 * int64(MaxVertices) * (sizeof(FVoxelLandMeshVertex) + sizeof(int32))
		+ 4 * int64(MaxTriangles) * (3 * sizeof(uint32) + 2 * sizeof(FVoxelLandM5ValidationTriangle) + sizeof(uint8))
		+ 4 * int64(256) * sizeof(FVoxelLandM5FixedTriangleQ18) + 1048576;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(4000000))
	{
		return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
			E::BudgetExceeded, TEXT("Mesh certification admission exhausted"));
	}
	FVoxelLandMeshingScratch MeshScratch;
	FVoxelLandMeshData Mesh;
	const EVoxelLandMeshResult MeshResult =
		VoxelLandMeshing::BuildSurfaceNets(Chunk, float(VoxelSizeCm), MeshScratch, Mesh, IsCancelled, bUnlimitedM4);
	if (MeshResult != EVoxelLandMeshResult::Success)
	{
		return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
			MeshResult == EVoxelLandMeshResult::Cancelled ? E::Cancelled : E::Unproven,
			TEXT("Actual CPU meshing did not succeed"));
	}
	TArray<FVoxelLandM5ValidationTriangle> Triangles;
	const auto Adapted = VoxelLand::M5::MeshAdapter::Adapt(Chunk, float(VoxelSizeCm), bUnlimitedM4, Mesh, MaxTriangles,
		32 * 32 * 32 * 3 + MaxTriangles, IsCancelled, Triangles);
	if (Adapted.Code != E::None)
	{
		return Adapted;
	}
	TArray<uint8> Selected;
	Selected.Init(0, Triangles.Num());
	TArray<FVoxelLandM5FixedTriangleQ18> Support;
	Support.Reserve(SupportIndices.Num());
	for (int32 Index : SupportIndices)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
				E::Cancelled, TEXT("Route certification cancelled"));
		}
		if (!Triangles.IsValidIndex(Index) || Selected[Index])
		{
			return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
				E::InvalidInput, TEXT("Invalid or duplicate support triangle"));
		}
		Selected[Index] = 1;
		FVoxelLandM5FixedTriangleQ18 Fixed;
		const auto Converted = VoxelLand::M5::FixedMesh::Convert(
			Triangles[Index], VoxelSizeCm, true, FVector3d::ZeroVector, Budget, IsCancelled, Fixed);
		if (Converted.Code != E::None)
		{
			return Converted;
		}
		Support.Add(Fixed);
	}
	constexpr int64 Q = int64(1) << 18;
	FVoxelLandM5FixedPointQ18 A, B;
	A.X = StartFeet.X * Q;
	A.Y = StartFeet.Y * Q;
	A.Z = StartFeet.Z * Q;
	B.X = EndFeet.X * Q;
	B.Y = EndFeet.Y * Q;
	B.Z = EndFeet.Z * Q;
	const auto Contact = VoxelLand::M5::FixedSupport::CertifyLinearContact(A, B, RadiusCm * Q, HeightCm * Q,
		ContactSkinCm * Q, MaxSlopeNumerator, MaxSlopeDenominator, Support, Budget, IsCancelled);
	if (Contact.Code != E::None)
	{
		return Contact;
	}
	TArray<FVoxelLandM5ValidationTriangle> Obstacles;
	Obstacles.Reserve(Triangles.Num() - Support.Num());
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & 63) == 0 && IsCancelled())
		{
			return FVoxelLandM5SingleChunkRouteInternals::SingleChunkRouteError(
				E::Cancelled, TEXT("Route certification cancelled"));
		}
		if (!Selected[Index])
		{
			Obstacles.Add(Triangles[Index]);
		}
	}
	// CPU mesh coordinates are exact inputs to this check. Renderer uncertainty
	// and later transforms remain outside this single-interval certificate.
	return VoxelLand::M5::SweptTriangle::Check(
		StartFeet, EndFeet, RadiusCm, HeightCm, MarginCm, FVector3d::ZeroVector, Obstacles, Budget, IsCancelled);
}
