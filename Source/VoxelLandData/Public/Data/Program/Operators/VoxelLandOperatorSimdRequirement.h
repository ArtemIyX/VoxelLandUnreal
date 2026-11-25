#pragma once

#include "CoreMinimal.h"

// SIMD providers opt in to a specific CPU baseline. None uses scalar only.
enum class EVoxelLandOperatorSimdRequirement : uint8
{
	None,
	SSE2
};
