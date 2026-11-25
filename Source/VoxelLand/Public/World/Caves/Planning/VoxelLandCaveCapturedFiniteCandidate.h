#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5CapturedBaseline;

// Finite source-bound proposal only: neither source freshness nor complete
// surface coverage, route safety, final field or world publication is certified.
class VOXELLAND_API FVoxelLandM5CapturedFiniteCandidate final
{
public:
	// Worker thread only. The source's immutable snapshot, budget, protection
	// mask and window are mandatory; no replacement inputs may be supplied.
	// MaxSamplesPerQuery must be positive and no larger than captured work cap.
	// Failure or cancellation preserves Out.
	static FVoxelLandM5Diagnostic Build(TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source,
		int64 MaxSamplesPerQuery, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5CapturedBaseline& GetSource() const { return *Source; }
	const FVoxelLandM5CandidateResult& GetCandidate() const { return *Candidate; }
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> GetSourceShared() const { return Source; }
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> GetCandidateShared() const
	{
		return Candidate.ToSharedRef();
	}

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5CapturedMeshRoutesFixture;
#endif
	FVoxelLandM5CapturedFiniteCandidate(TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> InSource,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
		: Source(MoveTemp(InSource))
		, Lease(MoveTemp(InLease))
	{
	}
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
};
