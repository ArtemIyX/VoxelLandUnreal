#pragma once
#include "CoreMinimal.h"
#include "Data/VoxelLandM5Handles.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5FiniteContributorSeal;

// Retains exact native owners. This is not contributor completeness or proof.
class VOXELLANDALGO_API FVoxelLandM5FieldInputs final
{
public:
	~FVoxelLandM5FieldInputs();
	FVoxelLandM5FieldInputs(const FVoxelLandM5FieldInputs&) = delete;
	FVoxelLandM5FieldInputs& operator=(const FVoxelLandM5FieldInputs&) = delete;
	static FVoxelLandM5Diagnostic Bind(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandM5BudgetRef SourceBudget,
		TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5FieldInputs>& Out);
	static FVoxelLandM5Diagnostic BindSealed(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandM5BudgetRef SourceBudget,
		TVoxelLandM5ConstRef<FVoxelLandM5FiniteContributorSeal> Seal, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM5FieldInputs>& Out);
	const FVoxelLandM5FiniteContributorSeal* GetSeal() const { return Seal.Get(); }
	const FVoxelLandM5BoundBaseline& GetBaseline() const { return Baseline.Get(); }
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> GetBaselineShared() const { return Baseline; }
	const FVoxelLandM5InfluenceSet& GetInfluence() const { return Influence.Get(); }
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> GetInfluenceShared() const { return Influence; }
	FVoxelLandM5BudgetRef GetSourceBudget() const { return SourceBudget; }
private:
	FVoxelLandM5FieldInputs(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TVoxelLandM5ConstPtr<FVoxelLandM5FiniteContributorSeal> InSeal);
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline;
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence;
	FVoxelLandM5BudgetRef SourceBudget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TVoxelLandM5ConstPtr<FVoxelLandM5FiniteContributorSeal> Seal;
};
