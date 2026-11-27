#pragma once

#include "CoreMinimal.h"

/** Only Ready authorizes the caller to use Chaos for this complete lease. */
enum class EVoxelLandChaosReadiness : uint8
{
	Disabled,
	Unsupported,
	Pending,
	Ready,
	Failed,
	BudgetExceeded
};
