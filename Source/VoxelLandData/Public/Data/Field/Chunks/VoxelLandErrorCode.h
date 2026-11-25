#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandErrorCode : uint8
{
	None,
	GameThreadNotAllowed,
	InvalidProgram,
	InvalidParameter,
	InvalidProfile,
	UnsupportedLod,
	BackendUnavailable,
	NativeLibraryUnavailable,
	InvalidContext,
	NonFiniteCoordinate,
	NonFiniteDensity,
	Cancelled,
	InvalidMathDomain
};
