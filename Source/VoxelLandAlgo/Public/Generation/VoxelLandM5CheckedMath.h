#pragma once

#include "CoreMinimal.h"

// General checked integer arithmetic; these helpers do not define a world
// coordinate scale, cave algorithm, or serialized representation.
// Every operation leaves Out unchanged when it returns false.
namespace VoxelLand::M5::CheckedMath
{
VOXELLANDALGO_API bool TryAdd(int64 A, int64 B, int64& Out);
VOXELLANDALGO_API bool TrySubtract(int64 A, int64 B, int64& Out);
VOXELLANDALGO_API bool TryMultiply(int64 A, int64 B, int64& Out);

// For cardinalities and byte counts: rejects negative inputs and overflow.
VOXELLANDALGO_API bool TryMultiplyNonNegative(int64 A, int64 B, int64& Out);

// Mathematical floor division. A zero or negative divisor is rejected.
VOXELLANDALGO_API bool TryFloorDivide(int64 Value, int64 PositiveDivisor, int64& Out);
}
