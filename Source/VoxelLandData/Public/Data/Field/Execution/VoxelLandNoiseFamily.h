#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandNoiseFamily : uint8
{
	Perlin,
	Fbm,
	Value,
	Simplex
};
