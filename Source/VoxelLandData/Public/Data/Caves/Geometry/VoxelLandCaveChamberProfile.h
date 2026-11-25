#pragma once

#include "CoreMinimal.h"

// Version 1 uses the legacy sphere; version 2 uses a horizontal circle with
// floor and ceiling planes in logical centimetres.
struct FVoxelLandM5ChamberProfile
{
	int32 Version = 1;
	int64 FloorCm = 0;
	int64 CeilingCm = 0;
};
