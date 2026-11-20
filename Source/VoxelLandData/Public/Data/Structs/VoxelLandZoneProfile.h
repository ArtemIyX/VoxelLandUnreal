#pragma once

#include "CoreMinimal.h"

struct FVoxelLandZoneProfile
{
	float VoxelSizeCm = 100.0f;
	TMap<FString, float> FloatOverrides;
	TMap<FString, int32> IntOverrides;
	TMap<FString, bool> BoolOverrides;
};
