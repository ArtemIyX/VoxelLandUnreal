#pragma once

#include "CoreTypes.h"

enum class EVoxelLandPCGTerrainDataStatus : uint8
{
	Pending,
	Ready,
	Empty,
	Failed,
	Unavailable,
};
