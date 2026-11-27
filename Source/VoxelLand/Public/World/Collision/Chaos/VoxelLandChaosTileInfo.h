#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChaosTileInfo
{
	int32 RequestedLod = INDEX_NONE;
	int32 InstalledLod = INDEX_NONE;
	uint64 SourceRevision = 0;
	bool bHasShape = false;
	bool bCoverageReady = false;
	bool bInstallOverBudget = false;
};
