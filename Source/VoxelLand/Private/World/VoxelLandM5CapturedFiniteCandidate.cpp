#include "World/VoxelLandM5CapturedFiniteCandidate.h"

#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FiniteEntrancePlanner.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedBaseline.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"

class FVoxelLandM5CapturedFiniteCandidateInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedFiniteCandidate");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5CapturedFiniteCandidate::Build(
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source,
	int64 MaxSamplesPerQuery, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::Cancelled, TEXT("Captured finite candidate cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::InvalidInput, TEXT("Captured candidate requires a worker thread"));
	}
	const FVoxelLandM5CapturedFiniteScratch& Captured = Source->GetCaptured();
	const FVoxelLandM5BaseCapture& Capture = Captured.GetCapture();
	const FVoxelLandM5WorldSnapshot& Snapshot = Capture.GetSnapshot();
	const FVoxelLandM5CaveSettings& Settings = Snapshot.Caves;
	if (!Settings.bEnabled || Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || MaxSamplesPerQuery < 1
		|| MaxSamplesPerQuery > Settings.Limits.MaxValidationWork)
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::InvalidInput, TEXT("Invalid finite mode or surface query limit"));
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Captured.GetBudget();
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::BudgetExceeded, TEXT("Captured candidate memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
	const FVoxelLandM5RegionCoord FiniteRegion;
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::CandidatePlanner::Build(
		Settings, Snapshot.Base.Seed, FiniteRegion, Budget, IsCancelled, Candidate);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Candidate)
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::Unproven, TEXT("Finite candidate missing"));
	}
	const TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection = Capture.GetProtectionGrid();
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Entered;
	Result = VoxelLand::M5::FiniteEntrancePlanner::Build(Settings, Snapshot.Base.Seed, Candidate.ToSharedRef(),
		Source->GetWindow(), Protection.Get(), Budget, MaxSamplesPerQuery, IsCancelled, Entered);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Entered)
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::Unproven, TEXT("Finite entrance candidate missing"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::Cancelled, TEXT("Captured finite candidate cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5CapturedFiniteCandidate(Source, MoveTemp(Lease)));
	Built->Candidate = MoveTemp(Entered);
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedFiniteCandidateInternals::Error(E::Cancelled, TEXT("Captured finite candidate cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
