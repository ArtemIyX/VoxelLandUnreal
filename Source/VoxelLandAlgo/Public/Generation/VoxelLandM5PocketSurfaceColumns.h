#pragma once

#include "CoreMinimal.h"
#include "Data/VoxelLandM5Handles.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/Function.h"

class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5BudgetLease;
class FVoxelLandWorkerContext;

// Complete vertical scans of a bounded XY tile, including its two-sample
// neighbor border. This is scratch evidence, not a final-field certificate.
class VOXELLANDALGO_API FVoxelLandM5PocketSurfaceColumns final
{
public:
	~FVoxelLandM5PocketSurfaceColumns();
	FVoxelLandM5PocketSurfaceColumns(const FVoxelLandM5PocketSurfaceColumns&) = delete;
	FVoxelLandM5PocketSurfaceColumns& operator=(const FVoxelLandM5PocketSurfaceColumns&) = delete;
	static FVoxelLandM5Diagnostic Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandWorkerContext& Worker,
		FVoxelLandM5PointCm OutputMinLattice, FIntPoint OutputSize, FVoxelLandM5BudgetRef Budget,
		TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5PocketSurfaceColumns>& Out);
	bool Matches(const FVoxelLandM5BoundBaseline& Baseline, const FVoxelLandM5InfluenceSet& Influence,
		const FVoxelLandM5Budget& Budget) const;
	bool GetSurfaceBounds(int64 LatticeX, int64 LatticeY, int64& OutMinCm, int64& OutMaxCm) const;
private:
	FVoxelLandM5PocketSurfaceColumns(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> OwnedBaseline;
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> OwnedInfluence;
	FVoxelLandM5BudgetRef OwnedBudget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	int64 TileMinX = 0, TileMinY = 0;
	int32 TileSizeX = 0, TileSizeY = 0;
	TArray<int64> Lower, Upper;
};
