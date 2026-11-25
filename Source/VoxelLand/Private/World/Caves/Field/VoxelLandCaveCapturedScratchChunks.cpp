#include "World/Caves/Field/VoxelLandCaveCapturedScratchChunks.h"

#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Field/VoxelLandCaveCapturedBaseline.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteCandidate.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"

class FVoxelLandM5CapturedScratchChunksInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.CapturedScratchChunks");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5CapturedScratchChunks::BuildNetwork(
	TSharedRef<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate, const FIntVector& MinChunk,
	const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out)
{
	return BuildInternal(Candidate->GetSourceShared(), Candidate, MinChunk, MaxChunk, MaxChunks, IsCancelled, Out);
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedScratchChunks::BuildPocketOnly(
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source, const FIntVector& MinChunk,
	const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out)
{
	return BuildInternal(Source, nullptr, MinChunk, MaxChunk, MaxChunks, IsCancelled, Out);
}

FVoxelLandM5Diagnostic FVoxelLandM5CapturedScratchChunks::BuildInternal(
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source,
	TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate, const FIntVector& MinChunk,
	const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::Cancelled, TEXT("Captured scratch chunks cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::InvalidInput, TEXT("Captured scratch chunks require a worker thread"));
	}
	const FVoxelLandM5CapturedFiniteScratch& Captured = Source->GetCaptured();
	const FVoxelLandM5BaseCapture& Capture = Captured.GetCapture();
	const FVoxelLandM5WorldSnapshot& Snapshot = Capture.GetSnapshot();
	const FVoxelLandM5CaveSettings& Settings = Snapshot.Caves;
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || (Candidate && !Settings.bEnabled)
		|| (!Candidate && (Settings.bEnabled || !Settings.Pockets.bEnabled)))
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::InvalidInput, TEXT("Captured scratch mode mismatch"));
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = Captured.GetBudget();
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096, Lease))
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::BudgetExceeded, TEXT("Captured chunk wrapper memory exhausted"));
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Plan;
	if (Candidate)
	{
		Plan = Candidate->GetCandidateShared();
	}
	TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
	FVoxelLandM5Diagnostic Result =
		FVoxelLandM5ScratchChunks::Build(Settings, Snapshot.Manifest, Snapshot.Base.Seed, Source->GetWindowShared(),
			Plan, Capture.GetProtectionGrid(), MinChunk, MaxChunk, MaxChunks, Budget, IsCancelled, Chunks);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Chunks)
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(E::Unproven, TEXT("Captured scratch chunks missing"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::Cancelled, TEXT("Captured scratch chunks cancelled"));
	}
	TSharedPtr<FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5CapturedScratchChunks(MoveTemp(Lease), Source));
	Built->Candidate = MoveTemp(Candidate);
	Built->Chunks = MoveTemp(Chunks);
	if (IsCancelled())
	{
		return FVoxelLandM5CapturedScratchChunksInternals::Error(
			E::Cancelled, TEXT("Captured scratch chunks cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
