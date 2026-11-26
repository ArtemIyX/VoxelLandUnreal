#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandCapsuleSweepStatus : uint8
{
	Clear,
	Contact,
	StartPenetrating,
	Unavailable,
	Indeterminate,
	Invalid
};
