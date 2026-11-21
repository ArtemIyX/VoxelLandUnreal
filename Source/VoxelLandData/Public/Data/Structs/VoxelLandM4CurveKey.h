#pragma once

#include "CoreMinimal.h"

// A normalized input and an output in the curve's caller-defined integer units.
// InputQ16 covers the closed range [0, 65535].
struct FVoxelLandM4CurveKey
{
	uint16 InputQ16 = 0;
	int32 Output = 0;
};
