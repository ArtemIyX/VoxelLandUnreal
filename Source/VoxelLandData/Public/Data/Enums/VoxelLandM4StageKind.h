#pragma once

#include "CoreMinimal.h"

// Execution scope in the experimental M4 dependency validator. Not a cooked format.
enum class EVoxelLandM4StageKind : uint8
{
	Global,
	Sample,
	Filter
};
