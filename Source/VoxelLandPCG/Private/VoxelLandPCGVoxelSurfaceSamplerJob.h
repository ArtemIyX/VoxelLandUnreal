#pragma once

#include "CoreMinimal.h"
#include "VoxelLandPCGWorkFence.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Placement/VoxelLandTerrainPlacementPoint.h"

class UPCGVoxelTerrainData;
class UVoxelLandPCGVoxelSurfaceSamplerSettings;

class FVoxelLandPCGVoxelSurfaceSamplerJob final
{
public:
	FVoxelLandPCGVoxelSurfaceSamplerJob(
		const UPCGVoxelTerrainData& Terrain, const UVoxelLandPCGVoxelSurfaceSamplerSettings& Settings);

	~FVoxelLandPCGVoxelSurfaceSamplerJob();

	void Run();
	void Cancel() { bCancelled.Store(true); }
	bool IsComplete() const { return bComplete.Load(); }
	bool IsCancelled() const { return bCancelled.Load() || WorkFence->IsCancelled(); }
	bool Succeeded() const { return bSucceeded; }
	TConstArrayView<FVoxelLandTerrainPlacementPoint> GetSamples() const { return Samples; }

private:
	void ReleaseWorker();

	TSharedRef<FVoxelLandPCGWorkFence, ESPMode::ThreadSafe> WorkFence;
	TSharedPtr<const FVoxelLandCollisionField, ESPMode::ThreadSafe> Field;
	TArray<FVoxelLandCollisionField::FChunkPtr> Chunks;
	FBox Bounds;
	double MinimumSpacingCm;
	double MaximumSlopeDegrees;
	int32 Seed;
	TAtomic<bool> bCancelled{false};
	TAtomic<bool> bComplete{false};
	bool bSucceeded = false;
	bool bHasReservation = true;
	TArray<FVoxelLandTerrainPlacementPoint> Samples;
};
