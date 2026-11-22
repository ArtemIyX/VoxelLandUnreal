#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5EntrancePlanner.h"
#include "Templates/SharedPointer.h"

// Owns scratch-only candidate data and its shared admission. The plan is
// destroyed before its lease. A separate copy must have its own reservation.
class VOXELLANDALGO_API FVoxelLandM5CandidateResult final
{
public:
	FVoxelLandM5CandidateResult(const FVoxelLandM5CandidateResult&) = delete;
	FVoxelLandM5CandidateResult& operator=(const FVoxelLandM5CandidateResult&) = delete;
	bool UsesBudget(const FVoxelLandM5Budget& Other) const { return &Budget.Get() == &Other; }
	// Exact field-by-field snapshot comparison; no digest collision is possible.
	bool MatchesSource(
		const FVoxelLandM5CaveSettings& Settings, int64 Seed, const FVoxelLandM5RegionCoord& Region) const;
	const FVoxelLandM5CavePlan& GetPlan() const { return Plan; }

private:
	friend FVoxelLandM5Diagnostic VoxelLand::M5::CandidatePlanner::Build(const FVoxelLandM5CaveSettings&, int64,
		const FVoxelLandM5RegionCoord&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
	friend FVoxelLandM5Diagnostic VoxelLand::M5::EntrancePlanner::Build(const FVoxelLandM5CaveSettings&, int64,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
			int64&, int64&, int64&, int32&, bool&, bool&)>,
		TFunctionRef<bool()>, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5EntranceFixtureFactory;
	friend class FVoxelLandM5TopologyFixtureFactory;
	friend class FVoxelLandM5HandshakeFixtureFactory;
#endif
	FVoxelLandM5CandidateResult(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		const FVoxelLandM5CaveSettings& InSettings, int64 InSeed, const FVoxelLandM5RegionCoord& InRegion,
		FVoxelLandM5CavePlan&& InPlan, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
		: Budget(MoveTemp(InBudget))
		, SourceSettings(InSettings)
		, SourceSeed(InSeed)
		, SourceRegion(InRegion)
		, Lease(MoveTemp(InLease))
		, Plan(MoveTemp(InPlan))
	{
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5CaveSettings SourceSettings;
	int64 SourceSeed;
	FVoxelLandM5RegionCoord SourceRegion;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandM5CavePlan Plan;
};
