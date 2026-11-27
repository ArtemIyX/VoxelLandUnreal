#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChaosStats
{
	int32 WaitingForField = 0;
	int32 MeshQueued = 0;
	int32 MeshActive = 0;
	int32 CookQueued = 0;
	int32 CookActive = 0;
	int32 ApplyQueued = 0;
	int32 ActiveBodies = 0;
	int32 DetachedCooks = 0;
	int64 PendingBytes = 0;
	int64 ResidentBytes = 0;
	int64 StaleResults = 0;
	int64 Failures = 0;
	int64 Holds = 0;
	int64 Recoveries = 0;
};
