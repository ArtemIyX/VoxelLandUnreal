#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandNoiseBackend : uint8
{
	Auto,
	Scalar,
	SSE2
};
