#include "Generation/VoxelLandM5ScratchRouteChecks.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5MultiRouteChecks.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ScratchChunks.h"

class FVoxelLandM5ScratchRouteChecksInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.ScratchRouteChecks");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchRouteChecks::CheckAll(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::Cancelled, TEXT("Scratch route checks cancelled"));
	}
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || !Chunks->IsOwnedBy(*Budget)
		|| !Window->IsOwnedBy(*Budget) || !Candidate->UsesBudget(*Budget)
		|| (Protection && !Protection->IsOwnedBy(*Budget)))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::InvalidInput, TEXT("Invalid finite route inputs or budget ownership"));
	}
	if (!Budget->TryChargeWork(128))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::BudgetExceeded, TEXT("Scratch route input work exhausted"));
	}
	const FVoxelLandM5RegionCoord& Region = Candidate->GetPlan().Region;
	if (!Chunks->MatchesDependencies(Window.Get(), Candidate.Get(), Protection.Get(), WorldSeed, Manifest)
		|| !Chunks->MatchesSettings(Settings) || !Candidate->MatchesSource(Settings, WorldSeed, Region))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::InvalidInput, TEXT("Scratch route sources do not match"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::Cancelled, TEXT("Scratch route checks cancelled"));
	}
	return VoxelLand::M5::MultiRouteChecks::CheckAll(Settings, WorldSeed, Region, Candidate, Chunks->GetChunks(),
		Chunks->GetMinInclusive(), Chunks->GetMaxExclusive(), Chunks->GetVoxelSizeCm(), false, ContactSkinCm,
		Selections, Budget, IsCancelled);
}
