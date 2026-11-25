#pragma once

#include "Containers/ArrayView.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainStamp.h"

struct FVoxelLandM6StampSettings
{
	static constexpr uint32 CurrentVersion = 1;
	static constexpr int32 MaxStamps = 16;

	// The caller keeps this view immutable during synchronous generator creation.
	// The successful generator owns a separate validated copy in array order.
	TConstArrayView<FVoxelLandM6Stamp> Stamps;
	uint32 Version = CurrentVersion;
	bool bEnabled = true;

	bool IsActive() const { return bEnabled && !Stamps.IsEmpty(); }
};
