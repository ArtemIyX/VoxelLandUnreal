#include "Data/Collision/Chaos/VoxelLandChaosCollisionSettings.h"

#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"

int32 FVoxelLandChaosCollisionSettings::GetLod(const EVoxelLandChaosCollisionDetail Detail)
{
	switch (Detail)
	{
		case EVoxelLandChaosCollisionDetail::LOD0:
			return 0;
		case EVoxelLandChaosCollisionDetail::LOD1:
			return 1;
		case EVoxelLandChaosCollisionDetail::LOD2:
			return 2;
		default:
			return INDEX_NONE;
	}
}

bool FVoxelLandChaosCollisionSettings::Validate(FString& OutError) const
{
	OutError.Reset();
	if (GetLod(FixedDetail) == INDEX_NONE
		|| (UnresolvedRenderDetail != EVoxelLandChaosCollisionDetail::Disabled
			&& GetLod(UnresolvedRenderDetail) == INDEX_NONE)
		|| RenderLodDetails.Num() != RenderLodCount)
	{
		OutError = TEXT("Chaos collision requires a valid fixed LOD, unresolved detail, and six render LOD entries.");
		return false;
	}
	for (const EVoxelLandChaosCollisionDetail Detail : RenderLodDetails)
	{
		if (Detail != EVoxelLandChaosCollisionDetail::Disabled && GetLod(Detail) == INDEX_NONE)
		{
			OutError = TEXT("A render LOD maps to an unknown Chaos collision detail.");
			return false;
		}
	}
	if (CollisionMode != ECollisionEnabled::QueryOnly && CollisionMode != ECollisionEnabled::PhysicsOnly
		&& CollisionMode != ECollisionEnabled::QueryAndPhysics)
	{
		OutError = TEXT("Choose Query Only, Physics Only, or Query and Physics for Chaos terrain collision.");
		return false;
	}
	return ValidateBudget(OutError) && ValidateResponses(OutError);
}

bool FVoxelLandChaosCollisionSettings::ValidateBudget(FString& OutError) const
{
	constexpr int32 MaxWorkers = 16;
	constexpr int32 MaxUpdates = 64;
	constexpr double MinTickMs = 0.01;
	constexpr double MaxTickMs = 16.0;
	constexpr double MaxGraceSeconds = 30.0;
	if (MaxLeasedTiles <= 0 || MaxLeasedTiles > MaxAllowedLeasedTiles || MaxQueuedMeshes <= 0
		|| MaxQueuedMeshes > MaxAllowedLeasedTiles || MaxMeshWorkers <= 0 || MaxMeshWorkers > MaxWorkers
		|| MaxConcurrentCooks <= 0 || MaxConcurrentCooks > MaxWorkers || MaxQueuedMeshBytes <= 0
		|| MaxPendingCookBytes <= 0 || MaxCookedBytes <= 0 || MaxPhysicsUpdatesPerTick <= 0
		|| MaxPhysicsUpdatesPerTick > MaxUpdates || !FMath::IsFinite(GameThreadBudgetMs)
		|| GameThreadBudgetMs < MinTickMs || GameThreadBudgetMs > MaxTickMs || !FMath::IsFinite(RetirementGraceSeconds)
		|| RetirementGraceSeconds < 0.0 || RetirementGraceSeconds > MaxGraceSeconds)
	{
		OutError = TEXT("Chaos terrain collision budget is outside its supported limits.");
		return false;
	}
	return true;
}

bool FVoxelLandChaosCollisionSettings::ValidateResponses(FString& OutError) const
{
	if (Responses.Num() > ECC_MAX)
	{
		OutError = TEXT("Chaos terrain collision responses exceed the number of collision channels.");
		return false;
	}
	TSet<FName> Channels;
	Channels.Reserve(Responses.Num());
	for (const FVoxelLandChaosCollisionResponse& Entry : Responses)
	{
		if (Entry.Channel.IsNone() || Channels.Contains(Entry.Channel)
			|| (Entry.Response != ECR_Block && Entry.Response != ECR_Ignore))
		{
			OutError = TEXT("Chaos terrain collision responses require distinct named channels and Block or Ignore.");
			return false;
		}
		Channels.Add(Entry.Channel);
	}
	return true;
}

EVoxelLandChaosCollisionDetail FVoxelLandChaosCollisionSettings::ResolveDetail(
	const int32 RenderLod, const FVoxelLandChaosCollisionOverride* Override) const
{
	using EDetail = EVoxelLandChaosCollisionDetail;
	using EMode = EVoxelLandChaosCollisionOverrideMode;
	if (!bEnabled || (Override && (!Override->IsValid() || Override->Mode == EMode::Disabled)))
	{
		return EDetail::Disabled;
	}
	const EDetail AuthoredDetail =
		bMapByRenderLod
			? (RenderLodDetails.IsValidIndex(RenderLod) ? RenderLodDetails[RenderLod] : UnresolvedRenderDetail)
			: FixedDetail;
	const EDetail PolicyDetail = GetLod(AuthoredDetail) == INDEX_NONE ? EDetail::Disabled : AuthoredDetail;
	if (!Override || Override->Mode == EMode::Inherit)
	{
		return PolicyDetail;
	}
	if (Override->bOverrideDetail)
	{
		return Override->Detail;
	}
	// Explicit enable opts the footprint into demand even where the render mapping opts out.
	if (PolicyDetail == EDetail::Disabled && GetLod(FixedDetail) != INDEX_NONE)
	{
		return FixedDetail;
	}
	return PolicyDetail;
}
