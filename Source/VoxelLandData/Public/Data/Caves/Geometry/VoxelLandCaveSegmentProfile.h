#pragma once

#include "CoreMinimal.h"

// Version 1 uses the legacy round segment; version 2 uses floor and ceiling
// planes in logical centimetres. Values are immutable plan data.
struct FVoxelLandM5SegmentProfile
{
	int32 Version = 1;
	int64 StartFloorCm = 0;
	int64 EndFloorCm = 0;
	int64 StartCeilingCm = 0;
	int64 EndCeilingCm = 0;
};
