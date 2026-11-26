#pragma once

#include "VoxelLandMaterialRuleInput.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandMaterialRuleInput : uint8
{
	ElevationCm UMETA(DisplayName = "Elevation (cm)"),
	SlopeDegrees UMETA(DisplayName = "Slope (degrees)")
};
