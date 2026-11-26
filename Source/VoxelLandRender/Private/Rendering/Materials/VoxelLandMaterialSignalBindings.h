#pragma once

#include "Data/Meshing/VoxelLandMaterialSignalSource.h"
#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"

class FVoxelLandGenerator;
class FVoxelLandMaterialGraphProgram;
class UVoxelLandWorldConfig;

class FVoxelLandMaterialSignalBindings final : public IVoxelLandMaterialSignalSource
{
public:
	static bool Capture(const UVoxelLandWorldConfig& Config, const FVoxelLandMaterialGraphProgram& Program,
		FVoxelLandWorldSignalSnapshot::FPtr World,
		TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error);
	bool SampleTerrain(FGuid OutputId, const FVector3d& Position, double& Out) const override;
	bool SampleWorld(FName Channel, const FVector2d& Position, double& Out) const override;

private:
	TMap<FGuid, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>> Terrain;
	FVoxelLandWorldSignalSnapshot::FPtr World;
	TSet<FGuid> IntegerOutputs;
	bool bUsesFixedLattice = false;
};
