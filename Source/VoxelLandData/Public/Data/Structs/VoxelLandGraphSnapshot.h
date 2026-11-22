#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphParameter.h"
#include "Data/Structs/VoxelLandGraphFunctionSnapshot.h"
#include "VoxelLandGraphSnapshot.generated.h"

// Record copy captured on the game thread; workers never access the source asset.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	int32 SourceVersion = 1;
	UPROPERTY()
	TArray<FVoxelLandGraphParameter> Parameters;
	UPROPERTY()
	TArray<FVoxelLandGraphNodeRecord> Nodes;
	UPROPERTY()
	TArray<FVoxelLandGraphLinkRecord> Links;
	UPROPERTY()
	TArray<FVoxelLandGraphFunctionSnapshot> Functions;
};
