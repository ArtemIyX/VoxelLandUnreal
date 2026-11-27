#include "VoxelLandPCGVoxelSurfaceSamplerElement.h"
#include "VoxelLandPCGNodes.h"
#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "VoxelLandPCGTerrainData.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "Data/PCGBasePointData.h"
#include "PCGContext.h"

bool FVoxelLandPCGVoxelSurfaceSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGVoxelTerrainData* Data = FVoxelLandPCGNodes::GetTerrain(*Context);
	const UVoxelLandPCGVoxelSurfaceSamplerSettings* Settings =
		Context->GetInputSettings<UVoxelLandPCGVoxelSurfaceSamplerSettings>();
	TArray<FVoxelLandTerrainPlacementPoint> Samples;
	const int32 PlacementSeed =
		Data && Settings
			? static_cast<int32>(HashCombineFast(GetTypeHash(Data->GetMaskSeed()), GetTypeHash(Settings->GetSeed())))
			: 0;
	if (!Data || !Settings
		|| !FVoxelLandTerrainPlacement::Sample(
			Data->GetPlacementInput(), [Data] { return Data->IsCancelled(); }, Settings->MinimumSpacingCm,
			PlacementSeed, Settings->MaximumSlopeDegrees, Samples))
	{
		if (Data && Data->IsCancelled())
		{
			return true;
		}
		return FVoxelLandPCGNodes::Fail(*Context,
			NSLOCTEXT("VoxelLandPCG", "SurfaceSampleFailed",
				"Voxel Surface Sampler needs ready terrain, valid spacing and slope, and a candidate count within the cell budget."));
	}
#if WITH_EDITOR
	Data->RecordCandidateDebug(Samples);
#endif
	UPCGBasePointData* Points = FVoxelLandPCGNodes::MakePoints(*Context, Samples.Num());
	FPCGPointTransform::ValueRange Transforms = Points->GetTransformValueRange();
	FPCGPointSeed::ValueRange Seeds = Points->GetSeedValueRange();
	const FVector Origin(Data->GetView().OriginLocation);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		Transforms[Index] = FTransform(Samples[Index].Position - Origin);
		Seeds[Index] = static_cast<int32>(Samples[Index].Seed);
	}
	if (!FVoxelLandPCGNodes::WriteSurfaceMetadata(*Points, Samples))
	{
		return FVoxelLandPCGNodes::Fail(
			*Context, NSLOCTEXT("VoxelLandPCG", "SurfaceMetadataFailed", "Cannot write voxel surface attributes."));
	}
	FVoxelLandPCGNodes::PublishPoints(*Context, *Points);
	return true;
}
