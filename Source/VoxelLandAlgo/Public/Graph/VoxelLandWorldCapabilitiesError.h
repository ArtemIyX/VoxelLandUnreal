#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandWorldCapabilityError : uint8
{
	None,
	InvalidInput,
	UnknownCapability,
	FiniteWorldRequired,
	BudgetExceeded,
	Cancelled
};
