#pragma once

#include "CoreMinimal.h"

// Unlimited mode is independent of the finite global erosion prepass.
enum class EVoxelLandM4DomainMode : uint8
{
	Finite,
	Unlimited
};
