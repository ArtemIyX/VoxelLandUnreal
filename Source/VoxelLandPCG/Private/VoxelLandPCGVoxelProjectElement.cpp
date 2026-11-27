#include "VoxelLandPCGVoxelProjectElement.h"
#include "VoxelLandPCGNodes.h"
#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "VoxelLandPCGTerrainData.h"
#include "VoxelLandPCGVoxelProjectSettings.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FVoxelLandPCGVoxelProjectElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGProjectPoints);
	const UPCGVoxelTerrainData* Data = FVoxelLandPCGNodes::GetTerrain(*Context);
	const UVoxelLandPCGVoxelProjectSettings* Settings = Context->GetInputSettings<UVoxelLandPCGVoxelProjectSettings>();
	constexpr double MaxProjectionCm = 100000.0;
	if (!Data || !Settings || Settings->Direction.ContainsNaN() || Settings->Direction.IsNearlyZero()
		|| !FMath::IsFinite(Settings->MaximumDistanceCm) || Settings->MaximumDistanceCm <= 0.0
		|| Settings->MaximumDistanceCm > MaxProjectionCm)
	{
		return FVoxelLandPCGNodes::Fail(*Context,
			NSLOCTEXT("VoxelLandPCG", "InvalidProjection", "Voxel Project requires ready terrain and a bounded ray."));
	}
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Inputs.IsEmpty())
	{
		return true;
	}
	TArray<FVoxelLandTerrainPlacementMesh> Meshes;
	if (!FVoxelLandTerrainPlacement::BuildMeshes(
			Data->GetPlacementInput(), [Data] { return Data->IsCancelled(); }, Meshes))
	{
		if (Data->IsCancelled())
		{
			return true;
		}
		return FVoxelLandPCGNodes::Fail(*Context, NSLOCTEXT("VoxelLandPCG", "ProjectBuildFailed",
													  "Voxel Project cannot build the canonical terrain surface."));
	}
	const FVector Direction = Settings->Direction.GetSafeNormal();
	const FVector Origin(Data->GetView().OriginLocation);
	// Projection touches triangle geometry per input. Keep this operator
	// bounded separately from the single-pass surface sampler.
	constexpr int32 PointBudget = 1024;
	int32 ProcessedPoints = 0;
	for (const FPCGTaggedData& Tagged : Inputs)
	{
		if (!ProjectInput(*Context, Tagged, *Data, Meshes, Direction, Origin, *Settings, PointBudget, ProcessedPoints))
		{
			return FVoxelLandPCGNodes::Fail(
				*Context, NSLOCTEXT("VoxelLandPCG", "ProjectInputFailed",
							  "Voxel Project reached unavailable terrain, invalid point data, or its point budget."));
		}
		if (Data->IsCancelled())
		{
			return true;
		}
	}
	return true;
}

bool FVoxelLandPCGVoxelProjectElement::ProjectInput(FPCGContext& Context, const FPCGTaggedData& Tagged,
	const UPCGVoxelTerrainData& Data, TConstArrayView<FVoxelLandTerrainPlacementMesh> Meshes, const FVector& Direction,
	const FVector& Origin, const UVoxelLandPCGVoxelProjectSettings& Settings, int32 PointBudget, int32& ProcessedPoints)
{
	const UPCGBasePointData* Input = Cast<UPCGBasePointData>(Tagged.Data);
	if (!Input || Input->GetNumPoints() > PointBudget - ProcessedPoints)
	{
		return false;
	}
	ProcessedPoints += Input->GetNumPoints();
	TArray<FVoxelLandTerrainPlacementPoint> Samples;
	TArray<int32> Indices;
	const FPCGPointTransform::ConstValueRange InputTransforms = Input->GetConstTransformValueRange();
	for (int32 Index = 0; Index < Input->GetNumPoints(); ++Index)
	{
		if (Data.IsCancelled())
		{
			return true;
		}
		const FVector Start = InputTransforms[Index].GetLocation() + Origin;
		FVoxelLandTerrainPlacementPoint Hit;
		const bool bHit = FVoxelLandTerrainPlacement::Project(
			Meshes, Start, Direction, Settings.MaximumDistanceCm, Settings.bGroundOnly, Hit);
		const double Travel = bHit ? FMath::Min(FVector::Dist(Start, Hit.Position), Settings.MaximumDistanceCm)
								   : Settings.MaximumDistanceCm;
		if (!FVoxelLandTerrainPlacement::CanProject(*Data.GetView().Field, Start, Direction, Travel))
		{
			return false;
		}
		if (bHit)
		{
			Indices.Add(Index);
			Samples.Add(Hit);
		}
	}
	UPCGBasePointData* Output = FVoxelLandPCGNodes::MakePoints(Context, Samples.Num(), Input);
	FPCGPointTransform::ValueRange Transforms = Output->GetTransformValueRange();
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		Input->CopyPointsTo(Output, Indices[Index], Index, 1);
		FTransform Transform = InputTransforms[Indices[Index]];
		Transform.SetLocation(Samples[Index].Position - Origin);
		Transforms[Index] = Transform;
	}
	if (!FVoxelLandPCGNodes::WriteSurfaceMetadata(*Output, Samples))
	{
		return false;
	}
	FVoxelLandPCGNodes::PublishPoints(Context, *Output, &Tagged);
	return true;
}
