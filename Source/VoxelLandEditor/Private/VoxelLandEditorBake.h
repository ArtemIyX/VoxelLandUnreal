#pragma once

#include "CoreMinimal.h"

class UVoxelLandWorldConfig;
class UVoxelLandM4ControlGraph;

namespace VoxelLandM4Editor
{
// Captures named source projections on the game thread, then compiles all three atomically.
bool CompileNamedGraph(UVoxelLandM4ControlGraph& Graph, FString& OutError);
// Must run on the game thread: captures authored UObject data before baking on a worker.
bool BakeWorldConfig(UVoxelLandWorldConfig& Config, FString& OutError);
}
