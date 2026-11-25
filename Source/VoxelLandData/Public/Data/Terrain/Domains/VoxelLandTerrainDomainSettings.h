#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainMode.h"
#include "VoxelLandTerrainDomainSettings.generated.h"

// Standalone proposed M4 policy; not yet attached to world settings.
// The finite defaults cover 16.384 km square about the world origin.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4DomainSettings
{
	GENERATED_BODY()
	UPROPERTY()
	EVoxelLandM4DomainMode Mode = EVoxelLandM4DomainMode::Finite;
	UPROPERTY()
	bool bGlobalErosion = true;
	UPROPERTY()
	int64 OriginXCm = -819200;
	UPROPERTY()
	int64 OriginYCm = -819200;
	UPROPERTY()
	int32 CellsX = 256;
	UPROPERTY()
	int32 CellsY = 256;
	UPROPERTY()
	int64 GridSpacingCm = 6400;
};
