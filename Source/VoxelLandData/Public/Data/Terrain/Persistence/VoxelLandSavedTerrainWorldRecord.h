#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "VoxelLandSavedTerrainWorldRecord.generated.h"

// Persist with an Unreal property archive. Contains copied control source,
// cooked programs, baked LUTs and complete generation settings; it holds no
// transient grid, provider instance or source asset pointer. Trusted save data
// only: the caller must impose an archive-size limit before deserialization.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4SavedWorld
{
	GENERATED_BODY()

	UPROPERTY()
	int32 FormatVersion = 1;

	UPROPERTY()
	FVoxelLandM4WorldSnapshot Snapshot;

	UPROPERTY()
	FVoxelLandM4IdentityRecord Identity;
};
