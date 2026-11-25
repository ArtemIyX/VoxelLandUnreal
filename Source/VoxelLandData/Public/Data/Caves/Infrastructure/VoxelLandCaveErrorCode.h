#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandM5ErrorCode : uint8
{
	None,
	InvalidInput,
	OutOfDomain,
	BudgetExceeded,
	Unproven,
	Blocked,
	UnsupportedVersion,
	ProviderUnavailable,
	Cancelled,
	ArithmeticOverflow,
	InconsistentTopology,
	IdentityCollision
};
