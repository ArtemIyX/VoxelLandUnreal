#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM4UnlimitedBackend.generated.h"

// Versioned terrain appearance; existing worlds use LegacyFloat.
UENUM(BlueprintType)
enum class EVoxelLandM4UnlimitedBackend : uint8
{
	Invalid = 0 UMETA(Hidden),
	LegacyFloat = 1 UMETA(DisplayName = "Legacy float controls"),
	FixedLattice = 2 UMETA(DisplayName = "Large-coordinate fixed lattice")
};
