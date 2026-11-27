#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandChaosPhase : uint8
{
	Field,
	Mesh,
	CookQueued,
	Cooking,
	Apply,
	Ready,
	Failed
};
