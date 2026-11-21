#pragma once

#include "CoreMinimal.h"

class UVoxelLandM4WorldConfig;

namespace VoxelLandM4Editor
{
// Must run on the game thread: captures authored UObject data before baking on a worker.
bool BakeWorldConfig(UVoxelLandM4WorldConfig& Config, FString& OutError);
}
