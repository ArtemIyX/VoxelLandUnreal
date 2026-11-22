#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Data/Structs/VoxelLandM5IdentityRecord.h"
#include "VoxelLandM5SavedWorld.generated.h"

// Pre-release property record only; no M5 restore path exists yet. Callers must
// bound trusted archive size before deserializing and revalidate all dependencies.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5SavedWorld
{
	GENERATED_BODY()
	UPROPERTY()
	int32 FormatVersion = 1;
	UPROPERTY()
	FVoxelLandM5WorldSnapshot Snapshot;
	UPROPERTY()
	FVoxelLandM5IdentityRecord Identity;
};
