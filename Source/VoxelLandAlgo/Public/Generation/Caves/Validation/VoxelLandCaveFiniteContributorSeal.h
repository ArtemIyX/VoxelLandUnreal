#pragma once
#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5BudgetLease;
class FVoxelLandWorkerContext;

// Canonical finite producer inventory only. This does not prove closed
// boundaries, entrance safety, route safety, final field or publication.
class VOXELLANDALGO_API FVoxelLandM5FiniteContributorSeal final
{
public:
	~FVoxelLandM5FiniteContributorSeal();
	FVoxelLandM5FiniteContributorSeal(const FVoxelLandM5FiniteContributorSeal&) = delete;
	FVoxelLandM5FiniteContributorSeal& operator=(const FVoxelLandM5FiniteContributorSeal&) = delete;
	static FVoxelLandM5Diagnostic Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandM5BudgetRef SourceBudget,
		FVoxelLandWorkerContext& Worker, int64 MaxSamplesPerQuery, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM5FiniteContributorSeal>& Out);
	bool Matches(const FVoxelLandM5BoundBaseline& InBaseline, const FVoxelLandM5InfluenceSet& InInfluence,
		const FVoxelLandM5Budget& InBudget) const;
	const FVoxelLandM5CandidateResult& GetCandidate() const { return Candidate.Get(); }
	TVoxelLandM5ConstRef<FVoxelLandM5CandidateResult> GetCandidateShared() const { return Candidate; }

private:
	FVoxelLandM5FiniteContributorSeal(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
		TVoxelLandM5ConstRef<FVoxelLandM5CandidateResult> InCandidate,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline;
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence;
	FVoxelLandM5BudgetRef Budget;
	TVoxelLandM5ConstRef<FVoxelLandM5CandidateResult> Candidate;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
};
