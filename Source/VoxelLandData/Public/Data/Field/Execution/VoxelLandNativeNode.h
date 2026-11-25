#pragma once

#include "CoreMinimal.h"

class FFastNoiseNative;

namespace VoxelLandNative
{
struct FNode
{
	FNode();
	~FNode();
	TUniquePtr<FFastNoiseNative> Generator;
	double MaximumPosition = 0.0;
};
}
