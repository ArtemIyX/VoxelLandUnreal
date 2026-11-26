#include "World/VoxelLandWorld.h"

#if !UE_BUILD_SHIPPING && !UE_SERVER

#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "VoxelLandClipmapRenderWork.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

class FVoxelLandClipmapFlightCoverage final
{
public:
	static constexpr int32 LodCount = 6;
	static constexpr int32 CameraProbeIndex = 1;
	static constexpr int32 FirstEdgeProbeIndex = 2;
	static constexpr int32 MovingProbeCount = 6;
	static constexpr double OuterProbeFraction = 0.999;

	static FString JoinIntegers(TConstArrayView<int32> Values)
	{
		FString Result;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Result.AppendChar(TEXT(','));
			}
			Result.AppendInt(Values[Index]);
		}
		return Result;
	}

	static TOptional<double> ProjectTriangle(
		const FVector2D& Point, const FVector3f& A, const FVector3f& B, const FVector3f& C)
	{
		const FVector2D AB(double(B.X) - A.X, double(B.Y) - A.Y);
		const FVector2D AC(double(C.X) - A.X, double(C.Y) - A.Y);
		const FVector2D AP(Point.X - A.X, Point.Y - A.Y);
		const double Area = FVector2D::CrossProduct(AB, AC);
		if (FMath::IsNearlyZero(Area, UE_DOUBLE_SMALL_NUMBER))
		{
			return {};
		}
		const double AlongB = FVector2D::CrossProduct(AP, AC) / Area;
		const double AlongC = FVector2D::CrossProduct(AB, AP) / Area;
		if (AlongB < -UE_DOUBLE_SMALL_NUMBER || AlongC < -UE_DOUBLE_SMALL_NUMBER
			|| AlongB + AlongC > 1.0 + UE_DOUBLE_SMALL_NUMBER)
		{
			return {};
		}
		return A.Z + AlongB * (double(B.Z) - A.Z) + AlongC * (double(C.Z) - A.Z);
	}

	static TOptional<double> FindHighestTerrain(const FVoxelLandMeshData& Mesh, const FVector2D& LocalPoint)
	{
		if (LocalPoint.X < Mesh.Bounds.Min.X || LocalPoint.X > Mesh.Bounds.Max.X || LocalPoint.Y < Mesh.Bounds.Min.Y
			|| LocalPoint.Y > Mesh.Bounds.Max.Y)
		{
			return {};
		}

		TOptional<double> Height;
		for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
		{
			const FVoxelLandMeshVertex& A = Mesh.Vertices[Mesh.Indices[Index]];
			const FVoxelLandMeshVertex& B = Mesh.Vertices[Mesh.Indices[Index + 1]];
			const FVoxelLandMeshVertex& C = Mesh.Vertices[Mesh.Indices[Index + 2]];
			const TOptional<double> Hit = ProjectTriangle(LocalPoint, A.Position, B.Position, C.Position);
			if (Hit.IsSet() && (!Height.IsSet() || Hit.GetValue() > Height.GetValue()))
			{
				Height = Hit;
			}
		}
		return Height;
	}
};

bool AVoxelLandWorld::DescribeClipmapFlightCoverage(
	const FVector& LogicalCamera, TConstArrayView<FVector2D> FixedProbes, uint64& LastTicket, FString& Out) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_CollectFlightCoverage);
	const VoxelLandWorldPrivate::FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	if (!Visual || !Visual->Published || Visual->PublishedTicket == LastTicket)
	{
		return false;
	}

	using FCoverage = FVoxelLandClipmapFlightCoverage;
	const VoxelLandWorldPrivate::FM7RenderRevision& Revision = *Visual->Published;
	const FVector& View = Revision.Layout.Settings.ViewCm;
	const FVector2D ViewXY(View.X, View.Y);
	const FVector2D CameraXY(LogicalCamera.X, LogicalCamera.Y);
	const double RadiusCm = Revision.Layout.Settings.HorizontalRadiusCm;
	const double OuterCm = RadiusCm * FCoverage::OuterProbeFraction;
	TArray<FVector2D, TInlineAllocator<16>> Probes;
	Probes.Add(ViewXY);
	Probes.Add(CameraXY);
	Probes.Add(ViewXY + FVector2D(OuterCm, 0.0));
	Probes.Add(ViewXY - FVector2D(OuterCm, 0.0));
	Probes.Add(ViewXY + FVector2D(0.0, OuterCm));
	Probes.Add(ViewXY - FVector2D(0.0, OuterCm));
	Probes.Append(FixedProbes.GetData(), FixedProbes.Num());
	TArray<double, TInlineAllocator<16>> Heights;
	Heights.Init(-TNumericLimits<double>::Max(), Probes.Num());
	TArray<int32, TInlineAllocator<16>> SurfaceLods;
	SurfaceLods.Init(INDEX_NONE, Probes.Num());
	int32 DrawCounts[FCoverage::LodCount] = {};

	for (const VoxelLandWorldPrivate::FM7Draw& Draw : Revision.Draws)
	{
		if (!Draw.Terrain || !Draw.Terrain->Bounds.IsValid || Draw.Key.LOD >= FCoverage::LodCount)
		{
			continue;
		}
		const FVoxelLandChunkKey RegionKey{
			VoxelLandCoordinates::RegionOf(Draw.Key.Coordinate, State->RegionDimensions), Draw.Key.LOD};
		const AVoxelLandRegion* Region = Visual->Regions.FindRef(RegionKey).Get();
		if (!Region || !Region->IsChunkEffectivelyVisible(Draw.Key.Coordinate))
		{
			continue;
		}
		++DrawCounts[Draw.Key.LOD];
		const FVector2D Origin(Draw.LogicalOrigin.X, Draw.LogicalOrigin.Y);
		for (int32 Index = 0; Index < Probes.Num(); ++Index)
		{
			const TOptional<double> LocalZ = FCoverage::FindHighestTerrain(*Draw.Terrain, Probes[Index] - Origin);
			if (LocalZ.IsSet() && LocalZ.GetValue() + Draw.LogicalOrigin.Z > Heights[Index])
			{
				Heights[Index] = LocalZ.GetValue() + Draw.LogicalOrigin.Z;
				SurfaceLods[Index] = Draw.Key.LOD;
			}
		}
	}

	Out = FString::Printf(TEXT("viewCm=(%.0f,%.0f,%.0f) radiusCm=%.0f leaves=%d refinements=%d terrain=[%s] near=%d "
							   "camera=%d cameraCm=(%.0f,%.0f,%.0f) lagXYCm=%.0f edgeCm=%.0f edges=[%s] fixed=[%s]"),
		View.X, View.Y, View.Z, RadiusCm, Revision.Layout.Leaves.Num(), Revision.RefinedLeaves,
		*FCoverage::JoinIntegers(MakeArrayView(DrawCounts)), SurfaceLods[0], SurfaceLods[FCoverage::CameraProbeIndex],
		LogicalCamera.X, LogicalCamera.Y, LogicalCamera.Z, (CameraXY - ViewXY).Size(), OuterCm,
		*FCoverage::JoinIntegers(MakeArrayView(SurfaceLods.GetData() + FCoverage::FirstEdgeProbeIndex,
			FCoverage::MovingProbeCount - FCoverage::FirstEdgeProbeIndex)),
		*FCoverage::JoinIntegers(
			MakeArrayView(SurfaceLods.GetData() + FCoverage::MovingProbeCount, FixedProbes.Num())));
	LastTicket = Visual->PublishedTicket;
	return true;
}

uint64 AVoxelLandWorld::GetSettledClipmapFlightTicket(const FVector& LogicalView) const
{
	const VoxelLandWorldPrivate::FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	if (!Visual || !Visual->Published || Visual->Published->Draws.IsEmpty())
	{
		return 0;
	}
	const FVoxelLandM7ClipmapSettings& Published = Visual->Published->Layout.Settings;
	const FVector Offset = Published.ViewCm - LogicalView;
	// The submitted horizontal view is rounded to the nearest eight voxels.
	constexpr double HalfViewStepInVoxels = 4.0;
	const double ToleranceCm = HalfViewStepInVoxels * Published.BaseVoxelSizeCm + UE_DOUBLE_SMALL_NUMBER;
	return FMath::Abs(Offset.X) <= ToleranceCm && FMath::Abs(Offset.Y) <= ToleranceCm ? Visual->PublishedTicket : 0;
}

#endif
