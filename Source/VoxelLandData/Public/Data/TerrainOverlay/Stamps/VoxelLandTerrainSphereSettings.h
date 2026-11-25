#pragma once

#include "Containers/ArrayView.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainSphereStamp.h"

struct FVoxelLandM6SphereSettings
{
	static constexpr uint32 CurrentVersion = 1;
	static constexpr int32 MaxStamps = 16;

	// The caller keeps this view immutable during synchronous generator creation.
	// The successful finite generator owns a separate validated copy.
	TConstArrayView<FVoxelLandM6SphereStamp> Stamps;
	uint32 Version = CurrentVersion;
	bool bEnabled = true;

	bool IsActive() const { return bEnabled && !Stamps.IsEmpty(); }
};
