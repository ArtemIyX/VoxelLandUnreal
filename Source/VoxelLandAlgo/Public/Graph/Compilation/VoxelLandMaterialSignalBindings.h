#pragma once

#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "Data/Meshing/VoxelLandMaterialSignalSource.h"
#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"

class FVoxelLandGenerator;
class FVoxelLandMaterialGraphProgram;
class UVoxelLandWorldConfig;
struct FVoxelLandTerrainSignalCapture;

class VOXELLANDALGO_API FVoxelLandMaterialSignalBindings final : public IVoxelLandMaterialSignalSource
{
public:
	static bool Capture(const UVoxelLandWorldConfig& Config, const FVoxelLandMaterialGraphProgram& Program,
		FVoxelLandWorldSignalSnapshot::FPtr World,
		TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error);
	static bool CaptureTerrain(
		const UVoxelLandWorldConfig& Config, FVoxelLandTerrainSignalCapture& Out, FString& Error);
	static bool BuildTerrain(const FVoxelLandTerrainSignalCapture& Capture,
		const TMap<FGuid, EVoxelLandGraphValueType>& Imported,
		TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error);
	bool SampleTerrain(FGuid OutputId, const FVector3d& Position, double& Out) const override;
	bool SampleWorld(FName Channel, const FVector2d& Position, double& Out) const override;

private:
	bool Build(const FVoxelLandTerrainSignalCapture& Capture, const TMap<FGuid, EVoxelLandGraphValueType>& Imported,
		FString& Error);
	TMap<FGuid, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>> Terrain;
	FVoxelLandWorldSignalSnapshot::FPtr World;
	TSet<FGuid> IntegerOutputs;
	bool bUsesFixedLattice = false;
	bool bUsesThreeDimensions = false;
};
