#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightGraphStageKind.h"

// Caller-classified node; dependencies are producer node IDs, not pin IDs.
struct FVoxelLandM4StageNode
{
	FGuid Id;
	EVoxelLandM4StageKind Stage = EVoxelLandM4StageKind::Global;
	bool bUsesZ = false;
	TArray<FGuid> Dependencies;
};
