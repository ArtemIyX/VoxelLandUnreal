#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandChunkClassification : uint8
{
	NeedsSamples,
	Empty,
	Solid,
	Surface
};
