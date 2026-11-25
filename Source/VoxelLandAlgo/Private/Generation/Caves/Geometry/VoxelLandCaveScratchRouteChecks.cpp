#include "Generation/Caves/Geometry/VoxelLandCaveScratchRouteChecks.h"

#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Persistence/VoxelLandCaveStageManifest.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiRouteChecks.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchMesh.h"
#include "Generation/Caves/Geometry/VoxelLandCaveRetainedRouteInputs.h"

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
	return CheckAll(Settings, WorldSeed, Manifest, Window, Candidate, Candidate, Protection, Chunks, ContactSkinCm,
		Selections, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchRouteChecks::CheckAll(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CompositionSource,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> RoutedCandidate,
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
		|| !Window->IsOwnedBy(*Budget) || !CompositionSource->UsesBudget(*Budget)
		|| !RoutedCandidate->UsesBudget(*Budget) || (Protection && !Protection->IsOwnedBy(*Budget)))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(
			E::InvalidInput, TEXT("Invalid finite route inputs or budget ownership"));
	}
	if (!Budget->TryChargeWork(128))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(
			E::BudgetExceeded, TEXT("Scratch route input work exhausted"));
	}
	const FVoxelLandM5RegionCoord& Region = CompositionSource->GetPlan().Region;
	if (!Chunks->MatchesDependencies(Window.Get(), CompositionSource.Get(), Protection.Get(), WorldSeed, Manifest)
		|| !Chunks->MatchesSettings(Settings) || !CompositionSource->MatchesSource(Settings, WorldSeed, Region)
		|| !RoutedCandidate->MatchesSource(Settings, WorldSeed, Region)
		|| (&RoutedCandidate.Get() != &CompositionSource.Get()
			&& !RoutedCandidate->IsDirectlyDerivedFrom(CompositionSource.Get())))
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(
			E::InvalidInput, TEXT("Scratch route sources do not match"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchRouteChecksInternals::Error(E::Cancelled, TEXT("Scratch route checks cancelled"));
	}
	return VoxelLand::M5::MultiRouteChecks::CheckAll(Settings, WorldSeed, Region, RoutedCandidate, Chunks->GetChunks(),
		Chunks->GetMinInclusive(), Chunks->GetMaxExclusive(), Chunks->GetVoxelSizeCm(), false, ContactSkinCm,
		Selections, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::ScratchRouteChecks::CheckAll(const FVoxelLandM5RetainedRouteInputs& Inputs,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5ScratchRouteChecksInternals;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Scratch route checks cancelled"));
	}
	const auto& Chunks = Inputs.Mesh.GetSource();
	const auto& Region = Inputs.CompositionSource->GetPlan().Region;
	if (Inputs.Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || !Inputs.Mesh.IsOwnedBy(*Budget)
		|| !Chunks.IsOwnedBy(*Budget) || !Inputs.Baseline.IsOwnedBy(*Budget)
		|| !Inputs.CompositionSource->UsesBudget(*Budget) || !Inputs.Routed->UsesBudget(*Budget)
		|| (Inputs.Protection && !Inputs.Protection->IsOwnedBy(*Budget)))
	{
		return I::Error(E::InvalidInput, TEXT("Invalid finite route inputs or budget ownership"));
	}
	if (!Budget->TryChargeWork(128))
	{
		return I::Error(E::BudgetExceeded, TEXT("Scratch route input work exhausted"));
	}
	if (!Chunks.MatchesDependencies(
			Inputs.Baseline, Inputs.CompositionSource.Get(), Inputs.Protection.Get(), Inputs.CaveSeed, Inputs.Manifest)
		|| !Chunks.MatchesSettings(Inputs.Settings)
		|| !Inputs.CompositionSource->MatchesSource(Inputs.Settings, Inputs.CaveSeed, Region)
		|| !Inputs.Routed->MatchesSource(Inputs.Settings, Inputs.CaveSeed, Region)
		|| !Inputs.Routed->IsDirectlyDerivedFrom(Inputs.CompositionSource.Get()))
	{
		return I::Error(E::InvalidInput, TEXT("Scratch route sources do not match"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Scratch route checks cancelled"));
	}
	return VoxelLand::M5::MultiRouteChecks::CheckAll(Inputs.Settings, Inputs.CaveSeed, Region, Inputs.Routed,
		Inputs.Mesh, Inputs.ContactSkinCm, Inputs.Selections, Budget, IsCancelled);
}
