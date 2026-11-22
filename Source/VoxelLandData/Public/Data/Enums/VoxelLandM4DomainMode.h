#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM4DomainMode.generated.h"

// Unlimited mode is independent of the finite global erosion prepass.
UENUM()
enum class EVoxelLandM4DomainMode : uint8
{
	Finite,
	Unlimited
};
