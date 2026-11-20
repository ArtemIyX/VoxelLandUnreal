#pragma once

#include "CoreMinimal.h"

// Fixed-order binary64 approximation with explicit binary32 nearest-even packing.
class VOXELLANDALGO_API FVoxelLandPower
{
public:
    static bool Evaluate(float Base, float Exponent, float& Result);
};
