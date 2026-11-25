#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

struct FVoxelLandM6FilterSettings;
struct FVoxelLandM6FilterStage;
struct FVoxelLandM6RedistanceSettings;
struct FVoxelLandM6StampSettings;
struct FVoxelLandM6WorldOverlay;

class VOXELLANDALGO_API FVoxelLandM6OverlayValidation final
{
public:
	// Value-only authoring preflight. The generator repeats admission when it copies the values.
	static FVoxelLandError Validate(const FVoxelLandM6WorldOverlay& Overlay, float VoxelSizeCm);
	static FVoxelLandError ValidateStamps(const FVoxelLandM6StampSettings& Settings, float VoxelSizeCm);
	static FVoxelLandError ValidateFilters(const FVoxelLandM6FilterSettings& Settings, float VoxelSizeCm);
	static FVoxelLandError ValidateRedistance(const FVoxelLandM6RedistanceSettings& Settings,
		TConstArrayView<FVoxelLandM6FilterStage> Filters, float VoxelSizeCm);

private:
	static FVoxelLandError ValidateVoxelSize(float VoxelSizeCm);
};
