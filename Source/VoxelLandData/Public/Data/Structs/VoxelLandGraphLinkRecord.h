#pragma once

#include "CoreMinimal.h"
#include "VoxelLandGraphLinkRecord.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphLinkRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid SourceNode;

	UPROPERTY()
	FGuid SourcePin;

	UPROPERTY()
	FGuid TargetNode;

	UPROPERTY()
	FGuid TargetPin;
};
