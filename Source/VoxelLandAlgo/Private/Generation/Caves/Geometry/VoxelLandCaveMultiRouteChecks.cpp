#include "Generation/Caves/Geometry/VoxelLandCaveMultiRouteChecks.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Geometry/VoxelLandCaveIdealRouteValidation.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiChunkRoute.h"

class FVoxelLandM5MultiRouteChecksInternals final
{
public:
	static FVoxelLandM5Diagnostic M5MultiRoutesFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = Code;
		Error.Stage = TEXT("M5.MultiRouteChecks");
		Error.Message = Message;
		return Error;
	}
};

static FVoxelLandM5Diagnostic CheckAllInternal(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TConstArrayView<FVoxelLandChunkData> Chunks, const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
	int32 VoxelSizeCm, bool bUnlimitedM4, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	const FVoxelLandM5ScratchMesh* RetainedMesh)
{
	using namespace VoxelLand::M5;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::Cancelled, TEXT("Route checks cancelled"));
	}
	if (Settings.Capsule.MaxSlopeDegrees < 0 || Settings.Capsule.MaxSlopeDegrees > 90 || ContactSkinCm < 0
		|| ContactSkinCm > 1048576 || Selections.IsEmpty() || Selections.Num() > Settings.Limits.MaxRouteLegs)
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::InvalidInput, TEXT("Invalid route-check settings or selections"));
	}
	const auto Ideal = IdealRouteValidation::Validate(Settings, WorldSeed, Region, Candidate, Budget, IsCancelled);
	if (Ideal.Code != E::None)
	{
		return Ideal;
	}
	const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
	int64 LegCount = 0;
	if (!Budget->TryChargeWork(int64(Plan.Routes.Num()) + 1))
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::BudgetExceeded, TEXT("Route count work limit exceeded"));
	}
	for (const FVoxelLandM5Route& Route : Plan.Routes)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
				E::Cancelled, TEXT("Route checks cancelled"));
		}
		if (!Route.bHasId || !CheckedMath::TryAdd(LegCount, Route.Legs.Num(), LegCount))
		{
			return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
				E::InvalidInput, TEXT("Invalid route ID or leg count"));
		}
	}
	if (LegCount != Selections.Num())
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::InvalidInput, TEXT("Every route leg requires exactly one selection"));
	}
	int64 Work = 0;
	if (!CheckedMath::TryMultiplyNonNegative(LegCount, Selections.Num(), Work)
		|| !CheckedMath::TryMultiplyNonNegative(Work, 3, Work)
		|| !CheckedMath::TryAdd(Work, 2 * int64(Plan.Routes.Num()) + LegCount + 1, Work))
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::ArithmeticOverflow, TEXT("Route selection work overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::BudgetExceeded, TEXT("Route selection work limit exceeded"));
	}
	for (const FVoxelLandM5Route& Route : Plan.Routes)
	{
		for (int32 LegIndex = 0; LegIndex < Route.Legs.Num(); ++LegIndex)
		{
			const FVoxelLandM5MultiRouteSupportSelection* Selected = nullptr;
			for (const FVoxelLandM5MultiRouteSupportSelection& Selection : Selections)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
						E::Cancelled, TEXT("Route checks cancelled"));
				}
				if (Selection.RouteId == Route.Id && Selection.LegIndex == LegIndex)
				{
					if (Selected)
					{
						return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
							E::InvalidInput, TEXT("Duplicate route support selection"));
					}
					Selected = &Selection;
				}
			}
			if (!Selected || Selected->SupportFaces.IsEmpty() || Selected->SupportFaces.Num() > 256)
			{
				return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
					E::InvalidInput, TEXT("Missing or invalid route support selection"));
			}
			const FVoxelLandM5RouteLeg& Leg = Route.Legs[LegIndex];
			// degrees/90 is a conservative lower bound for tan(degrees) on [0,90).
			const auto Check =
				RetainedMesh
					? MultiChunkRoute::Certify(*RetainedMesh, Leg.StartFeetCm, Leg.EndFeetCm, Settings.Capsule.RadiusCm,
						  Settings.Capsule.StandingHeightCm, Settings.Capsule.ClearanceMarginCm, ContactSkinCm,
						  Settings.Capsule.MaxSlopeDegrees, 90, Selected->SupportFaces, Budget, IsCancelled)
					: MultiChunkRoute::Certify(Chunks, MinInclusive, MaxExclusive, VoxelSizeCm, bUnlimitedM4,
						  Leg.StartFeetCm, Leg.EndFeetCm, Settings.Capsule.RadiusCm, Settings.Capsule.StandingHeightCm,
						  Settings.Capsule.ClearanceMarginCm, ContactSkinCm, Settings.Capsule.MaxSlopeDegrees, 90,
						  Selected->SupportFaces, Budget, IsCancelled);
			if (Check.Code != E::None)
			{
				return Check;
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5MultiRouteChecksInternals::M5MultiRoutesFailure(
			E::Cancelled, TEXT("Route checks cancelled"));
	}
	return FVoxelLandM5Diagnostic();
}

FVoxelLandM5Diagnostic VoxelLand::M5::MultiRouteChecks::CheckAll(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5RegionCoord& Region,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TConstArrayView<FVoxelLandChunkData> Chunks, const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
	int32 VoxelSizeCm, bool bUnlimitedM4, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return CheckAllInternal(Settings, WorldSeed, Region, Candidate, Chunks, MinInclusive, MaxExclusive, VoxelSizeCm,
		bUnlimitedM4, ContactSkinCm, Selections, Budget, IsCancelled, nullptr);
}

FVoxelLandM5Diagnostic VoxelLand::M5::MultiRouteChecks::CheckAll(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5RegionCoord& Region,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate, const FVoxelLandM5ScratchMesh& Mesh,
	int64 ContactSkinCm, TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return CheckAllInternal(Settings, WorldSeed, Region, Candidate, {}, FIntVector::ZeroValue, FIntVector::ZeroValue, 0,
		false, ContactSkinCm, Selections, Budget, IsCancelled, &Mesh);
}
