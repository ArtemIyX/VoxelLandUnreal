#include "World/VoxelLandM5CapturedSolidChecks.h"

#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ScratchProtectionCheck.h"
#include "Generation/VoxelLandM5ScratchRoofCheck.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedBaseline.h"
#include "World/VoxelLandM5CapturedFiniteCandidate.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5CapturedScratchChunks.h"

class FVoxelLandM5CapturedSolidChecksInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedSolidChecks");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5CapturedSolidChecks::Build(
	TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedSolidChecks, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedSolidChecksInternals::Error(E::Cancelled, TEXT("Captured solid checks cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedSolidChecksInternals::Error(E::InvalidInput, TEXT("Captured solid checks require a worker thread"));
	}
	const FVoxelLandM5CapturedBaseline& Baseline = Source->GetSource();
	const FVoxelLandM5CapturedFiniteScratch& Captured = Baseline.GetCaptured();
	const FVoxelLandM5BaseCapture& Capture = Captured.GetCapture();
	const FVoxelLandM5WorldSnapshot& Snapshot = Capture.GetSnapshot();
	const FVoxelLandM5CaveSettings& Settings = Snapshot.Caves;
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Captured.GetBudget();
	const TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection = Capture.GetProtectionGrid();
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
	if (const FVoxelLandM5CapturedFiniteCandidate* CapturedCandidate = Source->GetCandidate())
	{
		Candidate = CapturedCandidate->GetCandidateShared();
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedSolidChecksInternals::Error(E::BudgetExceeded, TEXT("Captured solid checks memory exhausted"));
	}
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::ScratchProtectionCheck::Check(Baseline.GetWindow(),
		Source->GetChunks(), Protection.Get(), Settings.Protection.bRequireMask, Settings.Protection.bProtectAll,
		Budget, IsCancelled);
	if (Result.Code != E::None)
	{
		return Result;
	}
	Result = VoxelLand::M5::ScratchRoofCheck::Check(Settings, Snapshot.Base.Seed, Snapshot.Manifest,
		Baseline.GetWindowShared(), Candidate, Protection, Source->GetChunksShared(), Budget, IsCancelled);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedSolidChecksInternals::Error(E::Cancelled, TEXT("Captured solid checks cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedSolidChecks, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5CapturedSolidChecks(MoveTemp(Lease), Source));
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedSolidChecksInternals::Error(E::Cancelled, TEXT("Captured solid checks cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
