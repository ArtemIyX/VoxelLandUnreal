#pragma once

#include "CoreMinimal.h"
#include "Data/World/Presentation/VoxelLandTerrainPresentation.h"

class UVoxelLandMaterialSetup;

class VOXELLANDRENDER_API FVoxelLandTerrainPresentationResolver
{
public:
	static FVoxelLandTerrainPresentation Resolve(
		UObject* Outer, const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath);
	static TArray<uint8> CaptureIdentity(const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath);
};
