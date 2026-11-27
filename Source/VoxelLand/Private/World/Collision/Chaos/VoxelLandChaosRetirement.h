#pragma once

#include "UObject/WeakObjectPtrTemplates.h"

class UVoxelLandChaosTerrainComponent;

struct FVoxelLandChaosRetirement
{
	TWeakObjectPtr<UVoxelLandChaosTerrainComponent> Body;
	int64 Bytes = 0;
};
