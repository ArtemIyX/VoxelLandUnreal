#pragma once
#include "CoreMinimal.h"
#include "Data/VoxelLandM5Handles.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5BudgetLease;
class FVoxelLandWorkerContext;

// Analytic strict-positive M4 lattice interval inside the supported generator
// Z domain. It does not prove an exterior boundary, reachability or publication.
class VOXELLANDALGO_API FVoxelLandM4AirContinuation final
{
public:
	~FVoxelLandM4AirContinuation();
	FVoxelLandM4AirContinuation(const FVoxelLandM4AirContinuation&) = delete;
	FVoxelLandM4AirContinuation& operator=(const FVoxelLandM4AirContinuation&) = delete;
	static FVoxelLandM5Diagnostic Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandM5BudgetRef SourceBudget,
		FVoxelLandWorkerContext& Worker, FIntPoint MinXY, FIntPoint MaxXYExclusive,
		FVoxelLandM5BudgetRef JobBudget, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM4AirContinuation>& Out);
	bool Matches(const FVoxelLandM5BoundBaseline& Baseline, const FVoxelLandM5InfluenceSet& Influence,
		const FVoxelLandM5Budget& SourceBudget, const FVoxelLandM5Budget& JobBudget) const;
	FIntPoint GetMinXY() const { return XYMin; }
	FIntPoint GetMaxXYExclusive() const { return XYMax; }
	int64 GetFirstLatticeZ() const { return FirstZ; }
	int64 GetLastLatticeZ() const { return LastZ; }
private:
	FVoxelLandM4AirContinuation(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSource,
		FVoxelLandM5BudgetRef InJob, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> OwnedBaseline;
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> OwnedInfluence;
	FVoxelLandM5BudgetRef OwnedSource, OwnedJob;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FIntPoint XYMin, XYMax;
	int64 FirstZ = 0, LastZ = 0;
};
