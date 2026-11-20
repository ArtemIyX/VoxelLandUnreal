#pragma once

#include "CoreMinimal.h"

enum class EVoxelLandVMBackend : uint8
{
	Auto,
	Scalar,
	Simd4
};
