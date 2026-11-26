#pragma once

#include "CoreMinimal.h"
#include "Data/World/Presentation/VoxelLandTerrainPresentation.h"

class UVoxelLandMaterialSetup;
class UVoxelLandWorldConfig;
class FVoxelLandWorldSignalSnapshot;

class VOXELLANDRENDER_API FVoxelLandTerrainPresentationResolver
{
public:
	static FVoxelLandTerrainPresentation Resolve(
		UObject* Outer, const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath);
	static TArray<uint8> CaptureIdentity(const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath);
	static FVoxelLandTerrainPresentation Resolve(UObject* Outer, const UVoxelLandWorldConfig& Config,
		TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> WorldSignals = nullptr);
	static TArray<uint8> CaptureIdentity(const UVoxelLandWorldConfig& Config);

private:
	static FVoxelLandTerrainPresentation ResolveSetup(UObject* Outer,
		const TSoftObjectPtr<UVoxelLandMaterialSetup>& SetupPath, bool bUseRules, float OcclusionStrength);
};
