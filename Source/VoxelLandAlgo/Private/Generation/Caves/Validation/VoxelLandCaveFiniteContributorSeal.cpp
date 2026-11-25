#include "Generation/Caves/Validation/VoxelLandCaveFiniteContributorSeal.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidatePlanner.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Planning/VoxelLandCaveFiniteEntrancePlanner.h"
#include "Generation/Caves/Field/VoxelLandCaveInfluenceSet.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"

namespace
{
FVoxelLandM5Diagnostic SealError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.FiniteContributorSeal");
	Result.Message = Message;
	return Result;
}
}
FVoxelLandM5FiniteContributorSeal::FVoxelLandM5FiniteContributorSeal(
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
	TVoxelLandM5ConstRef<FVoxelLandM5CandidateResult> InCandidate,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Baseline(MoveTemp(InBaseline))
	, Influence(MoveTemp(InInfluence))
	, Budget(MoveTemp(InBudget))
	, Candidate(MoveTemp(InCandidate))
	, Lease(MoveTemp(InLease))
{
}
FVoxelLandM5FiniteContributorSeal::~FVoxelLandM5FiniteContributorSeal() = default;
bool FVoxelLandM5FiniteContributorSeal::Matches(const FVoxelLandM5BoundBaseline& InBaseline,
	const FVoxelLandM5InfluenceSet& InInfluence, const FVoxelLandM5Budget& InBudget) const
{
	return &Baseline.Get() == &InBaseline && &Influence.Get() == &InInfluence && &Budget.Get() == &InBudget;
}
FVoxelLandM5Diagnostic FVoxelLandM5FiniteContributorSeal::Build(
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
	FVoxelLandWorkerContext& Worker, int64 MaxSamplesPerQuery, TFunctionRef<bool()> IsCancelled,
	TVoxelLandM5ConstPtr<FVoxelLandM5FiniteContributorSeal>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return SealError(E::Cancelled, TEXT("Contributor production cancelled"));
	}
	if (&InBaseline->GetDefinition() != &InInfluence->GetDefinition() || !InBaseline->UsesBudget(*InBudget)
		|| !InInfluence->UsesBudget(*InBudget) || !InBaseline->GetGenerator().OwnsWorkerOnCurrentThread(Worker))
	{
		return SealError(E::InvalidInput, TEXT("Contributor owner or worker mismatch"));
	}
	const auto& Snapshot = InBaseline->GetDefinition().GetSnapshot();
	if (!Snapshot.Caves.bEnabled || Snapshot.Caves.PlanningMode != EVoxelLandM5PlanningMode::Finite
		|| MaxSamplesPerQuery < 1 || MaxSamplesPerQuery > Snapshot.Caves.Limits.MaxValidationWork)
	{
		return SealError(E::InvalidInput, TEXT("Unsupported finite contributor inputs"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SealLease;
	if (!InBudget->TryChargeWork(1)
		|| !InBudget->TryReserveBytes(sizeof(FVoxelLandM5FiniteContributorSeal) + 4096, SealLease))
	{
		return SealError(E::BudgetExceeded, TEXT("Contributor seal admission exhausted"));
	}
	FVoxelLandM5CaveSettings Settings = Snapshot.Caves;
	Settings.Bounds = InInfluence->GetDeclaredBounds();
	const FVoxelLandM5RegionCoord Region;
	TVoxelLandM5ConstPtr<FVoxelLandM5CandidateResult> CandidateResult;
	auto Result = VoxelLand::M5::CandidatePlanner::Build(
		Settings, Snapshot.Base.Seed, Region, Snapshot.Manifest.Geometry, InBudget, IsCancelled, CandidateResult);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!CandidateResult || !CandidateResult->MatchesSource(Settings, Snapshot.Base.Seed, Region))
	{
		return SealError(E::Unproven, TEXT("Canonical finite candidate missing"));
	}
	if (Settings.Topology.EntranceCount > 0)
	{
		// A complete post-filter vertical scan cannot be established at both
		// authored Z endpoints when smoothing needs samples outside the source.
		if (Snapshot.Base.Smooth.bEnabled && Snapshot.Base.Smooth.Strength > 0.f)
		{
			return SealError(E::Unproven, TEXT("Complete smoothed entrance column is unavailable"));
		}
		const int64 V = Snapshot.Base.VoxelSizeCm;
		if (V <= 0)
		{
			return SealError(E::InvalidInput, TEXT("Invalid entrance lattice"));
		}
		const auto& Read = InInfluence->GetCoverage().ReadBoundsCm;
		// ReadBounds is the raw M4 input envelope. BuildBox returns post-filter
		// samples and supplies its own slope stencil, so do not expand twice.
		const int64 SlopeHalo = Snapshot.Base.bNormalizeSlope ? V : 0;
		int64 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
		if (!TryAdd(Read.MinInclusive.X, SlopeHalo, MinX) || !TryAdd(Read.MinInclusive.Y, SlopeHalo, MinY)
			|| !TrySubtract(Read.MaxExclusive.X - 1, SlopeHalo, MaxX)
			|| !TrySubtract(Read.MaxExclusive.Y - 1, SlopeHalo, MaxY))
		{
			return SealError(E::ArithmeticOverflow, TEXT("Entrance XY stencil overflow"));
		}
		const int64 MinCm[3] = {MinX, MinY, Snapshot.Caves.Bounds.MinInclusive.Z};
		const int64 MaxCm[3] = {MaxX, MaxY, Snapshot.Caves.Bounds.MaxExclusive.Z - 1};
		FVoxelLandM5PointCm Min;
		int64 MinLattice[3] = {}, MaxLattice[3] = {}, Count = 1;
		FIntVector Size;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (!TryFloorDivide(MinCm[Axis], V, MinLattice[Axis]) || !TryFloorDivide(MaxCm[Axis], V, MaxLattice[Axis]))
			{
				return SealError(E::ArithmeticOverflow, TEXT("Entrance lattice conversion failed"));
			}
			if (MinCm[Axis] % V != 0)
			{
				++MinLattice[Axis];
			}
			int64 Extent = 0;
			if (!TrySubtract(MaxLattice[Axis], MinLattice[Axis], Extent) || !TryAdd(Extent, 1, Extent) || Extent < 1
				|| Extent > 4096 || !TryMultiplyNonNegative(Count, Extent, Count) || Count > MAX_int32)
			{
				return SealError(E::Unproven, TEXT("Complete entrance window exceeds bounded box limits"));
			}
			Size[Axis] = int32(Extent);
		}
		Min.X = MinLattice[0];
		Min.Y = MinLattice[1];
		Min.Z = MinLattice[2];
		TVoxelLandM5ConstPtr<FVoxelLandM5BaselineWindow> Window;
		Result = FVoxelLandM5BaselineWindow::BuildBox(
			InBaseline->GetGeneratorShared(), Worker, Min, Size, InBudget, IsCancelled, Window);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (!Window)
		{
			return SealError(E::Unproven, TEXT("Complete entrance window missing"));
		}
		TVoxelLandM5ConstPtr<FVoxelLandM5CandidateResult> Entered;
		Result = VoxelLand::M5::FiniteEntrancePlanner::Build(Settings, Snapshot.Base.Seed,
			CandidateResult.ToSharedRef(), *Window, InBaseline->GetDefinition().GetProtection(), InBudget,
			MaxSamplesPerQuery, IsCancelled, Entered);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (!Entered || !Entered->MatchesSource(Settings, Snapshot.Base.Seed, Region))
		{
			return SealError(E::Unproven, TEXT("Entered finite candidate missing"));
		}
		CandidateResult = MoveTemp(Entered);
	}
	if (IsCancelled())
	{
		return SealError(E::Cancelled, TEXT("Contributor production cancelled"));
	}
	Out = MakeShareable(new FVoxelLandM5FiniteContributorSeal(
		InBaseline, InInfluence, InBudget, CandidateResult.ToSharedRef(), MoveTemp(SealLease)));
	return {};
}
