#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "VoxelLandSavedTerrainWorldRecord.generated.h"

// Persist with an Unreal property archive. The authored format contains a
// projected source graph and generation settings, without transient grids or
// provider instances. Legacy payload members remain readable. Trusted save data
// only: the caller must impose an archive-size limit before deserialization.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4SavedWorld
{
	GENERATED_BODY()

	static constexpr int32 AuthoredFormatVersion = 2;

	// Keep the legacy default for property archives that omitted the field.
	UPROPERTY()
	int32 FormatVersion = 1;

	UPROPERTY()
	FVoxelLandM4WorldSnapshot Snapshot;

	UPROPERTY()
	FVoxelLandM4IdentityRecord Identity;
};
