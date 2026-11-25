#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidatePlanner.h"
#include "Generation/Caves/Planning/VoxelLandCaveEntrancePlanner.h"
#include "Generation/Caves/Geometry/VoxelLandCaveRouteBuilder.h"
#include "Templates/SharedPointer.h"

// Owns scratch-only candidate data and its shared admission. The plan is
// destroyed before its lease. A separate copy must have its own reservation.
class VOXELLANDALGO_API FVoxelLandM5CandidateResult final
{
public:
	FVoxelLandM5CandidateResult(const FVoxelLandM5CandidateResult&) = delete;
	FVoxelLandM5CandidateResult& operator=(const FVoxelLandM5CandidateResult&) = delete;
	bool UsesBudget(const FVoxelLandM5Budget& Other) const { return &Budget.Get() == &Other; }
	// Exact fixed-size settings comparison, also used when no network exists.
	static bool SameSettings(const FVoxelLandM5CaveSettings& A, const FVoxelLandM5CaveSettings& B);
	// Exact field-by-field snapshot comparison; no digest collision is possible.
	bool MatchesSource(
		const FVoxelLandM5CaveSettings& Settings, int64 Seed, const FVoxelLandM5RegionCoord& Region) const;
	const FVoxelLandM5CavePlan& GetPlan() const { return Plan; }
	// Full ordered bytes bind every plan field. Empty means not finalized.
	TConstArrayView<uint8> GetCanonicalPlanBytes() const { return CanonicalPlanBytes; }
	// Exact source budget and both retained admissions are required.
	bool HasOwnedCanonicalPlan(const FVoxelLandM5Budget& Owner) const
	{
		return UsesBudget(Owner) && Lease.IsValid() && CanonicalPlanLease.IsValid() && Owner.OwnsLease(*Lease)
			&& Owner.OwnsLease(*CanonicalPlanLease) && !CanonicalPlanBytes.IsEmpty();
	}
	// True only for a direct RouteBuilder result of this exact source object.
	bool IsDirectlyDerivedFrom(const FVoxelLandM5CandidateResult& Source) const
	{
		return DirectSource.Get() == &Source;
	}

private:
	friend FVoxelLandM5Diagnostic VoxelLand::M5::RouteBuilder::Build(const FVoxelLandM5CaveSettings&, int64,
		const FVoxelLandM5RegionCoord&, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>,
		TConstArrayView<FVoxelLandM5RouteAnchorChoice>, TConstArrayView<FVoxelLandM5RouteConnectionChoice>,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
	friend FVoxelLandM5Diagnostic VoxelLand::M5::CandidatePlanner::Build(const FVoxelLandM5CaveSettings&, int64,
		const FVoxelLandM5RegionCoord&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
	friend FVoxelLandM5Diagnostic VoxelLand::M5::CandidatePlanner::Build(const FVoxelLandM5CaveSettings&, int64,
		const FVoxelLandM5RegionCoord&, int32, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
	friend FVoxelLandM5Diagnostic VoxelLand::M5::EntrancePlanner::Build(const FVoxelLandM5CaveSettings&, int64,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
			int64&, int64&, int64&, int32&, bool&, bool&)>,
		TFunctionRef<bool()>, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>&);
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5SupportSelectorFixture;
	friend class FVoxelLandM5ScratchRouteLineageFixture;
	friend class FVoxelLandM5CapturedMeshRoutesFixture;
	friend class FVoxelLandM5FiniteRoutePlannerFixtureFactory;
	friend class FVoxelLandM5MultiRoutesFixtureFactory;
	friend class FVoxelLandM5EntranceMaskFixtureFactory;
	friend class FVoxelLandM5GatedEntranceFixtureFactory;
	friend class FVoxelLandM5ChunkRoutesFixtureFactory;
	friend class FVoxelLandM5IdealRouteFixtureFactory;
	friend class FVoxelLandM5RouteBuilderFixtureFactory;
	friend class FVoxelLandM5EntranceFixtureFactory;
	friend class FVoxelLandM5FiniteEntranceFixtureFactory;
	friend class FVoxelLandM5TopologyFixtureFactory;
	friend class FVoxelLandM5HandshakeFixtureFactory;
	friend class FVoxelLandM5RouteTopologyFixtureFactory;
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
	FVoxelLandM5Diagnostic BindCanonicalPlan(TFunctionRef<bool()> IsCancelled);
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5CaveSettings SourceSettings;
	int64 SourceSeed;
	FVoxelLandM5RegionCoord SourceRegion;
	// Direct source remains alive until after this result's plan and lease.
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> DirectSource;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> CanonicalPlanLease;
	TArray<uint8> CanonicalPlanBytes;
	FVoxelLandM5CavePlan Plan;
};
