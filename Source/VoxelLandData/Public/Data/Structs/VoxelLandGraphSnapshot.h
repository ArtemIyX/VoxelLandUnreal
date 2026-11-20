#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphParameter.h"

// Record copy captured on the game thread; workers never access the source asset.
struct VOXELLANDDATA_API FVoxelLandGraphSnapshot
{
	int32 SourceVersion = 1;
	TArray<FVoxelLandGraphParameter> Parameters;
	TArray<FVoxelLandGraphNodeRecord> Nodes;
	TArray<FVoxelLandGraphLinkRecord> Links;
};
