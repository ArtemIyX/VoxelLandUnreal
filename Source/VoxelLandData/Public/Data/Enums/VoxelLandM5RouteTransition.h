#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandM5RouteTransition : uint8
{
	// Continuous supported travel; support and clearance must be proven on the final mesh.
	Walk,
	// Explicit swept leg of a step. The validator must prove clearance and support
	// at the step's entry and exit; its motion is not inferred from a height delta.
	Step
};
