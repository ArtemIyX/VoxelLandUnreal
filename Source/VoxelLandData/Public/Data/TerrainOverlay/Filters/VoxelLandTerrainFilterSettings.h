#pragma once

#include "Containers/ArrayView.h"
#include "Data/TerrainOverlay/Filters/VoxelLandTerrainFilterStage.h"

struct FVoxelLandM6FilterSettings
{
	static constexpr uint32 CurrentVersion = 1;
	static constexpr int32 MaxStages = 8;
	static constexpr int32 MaxHalo = 8;

	// Keep this view immutable during synchronous creation. The resulting
	// generator owns a separate validated copy in array order.
	TConstArrayView<FVoxelLandM6FilterStage> Stages;
	uint32 Version = CurrentVersion;
	bool bEnabled = true;

	bool IsActive() const { return bEnabled && !Stages.IsEmpty(); }
};
