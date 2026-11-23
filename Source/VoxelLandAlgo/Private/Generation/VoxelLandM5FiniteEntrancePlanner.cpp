#include "Generation/VoxelLandM5FiniteEntrancePlanner.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5EntrancePlanner.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5WindowSurfaceQuery.h"

class FVoxelLandM5FiniteEntrancePlannerInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FiniteEntrancePlanner");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::FiniteEntrancePlanner::Build(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5BaselineWindow& Window, const FVoxelLandM5ProtectionGrid* Protection,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, int64 MaxSamplesPerQuery,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteEntrancePlannerInternals::Error(E::Cancelled, TEXT("Finite entrance planning cancelled"));
	}
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || MaxSamplesPerQuery < 0
		|| !Candidate->UsesBudget(*Budget) || !Window.IsOwnedBy(*Budget)
		|| (Protection && !Protection->IsOwnedBy(*Budget)))
	{
		return FVoxelLandM5FiniteEntrancePlannerInternals::Error(E::InvalidInput, TEXT("Invalid finite entrance inputs or budget ownership"));
	}
	if (Settings.Water.Policy != EVoxelLandM5WaterPolicy::DryWorld)
	{
		return FVoxelLandM5FiniteEntrancePlannerInternals::Error(E::Unproven, TEXT("Finite entrance planning requires a dry-world declaration"));
	}
	if (Settings.Protection.bRequireMask && !Protection)
	{
		return FVoxelLandM5FiniteEntrancePlannerInternals::Error(E::Unproven, TEXT("Required protection grid is missing"));
	}

	// EntrancePlanner calls this borrowed function synchronously. Both retained
	// inputs and the callback remain alive throughout the call.
	const auto Query = [&Window, Protection, &Settings, MaxSamplesPerQuery, &IsCancelled](
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> QueryBudget, int64 X, int64 Y, int32 Radius,
		int64& SurfaceZ, int64& MinSurfaceZ, int64& MaxSurfaceZ, int32& Slope, bool& bProtected, bool& bFlooded) {
		return VoxelLand::M5::WindowSurfaceQuery::QueryFlat(Window, Protection, Settings.Protection.bProtectAll, true,
			QueryBudget, X, Y, Radius, MaxSamplesPerQuery, IsCancelled, SurfaceZ, MinSurfaceZ, MaxSurfaceZ, Slope,
			bProtected, bFlooded);
	};
	return VoxelLand::M5::EntrancePlanner::Build(Settings, WorldSeed, Candidate, Budget, Query, IsCancelled, Out);
}
