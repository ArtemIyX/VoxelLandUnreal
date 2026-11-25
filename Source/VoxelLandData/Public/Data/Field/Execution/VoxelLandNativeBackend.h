#pragma once

#include "CoreMinimal.h"

namespace VoxelLandNative
{
enum class EBackend : uint8
{
	Auto,
	Scalar,
	SSE2
};
}
