#pragma once

#include "Data/Collision/Chaos/VoxelLandChaosCollisionDetail.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionResponse.h"
#include "VoxelLandChaosCollisionSettings.generated.h"

struct FVoxelLandChaosCollisionOverride;

USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandChaosCollisionSettings
{
	GENERATED_BODY()

	static constexpr int32 RenderLodCount = 6;
	static constexpr int32 MaxAllowedLeasedTiles = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (DisplayName = "Enable Chaos Terrain Collision"))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	bool bMapByRenderLod = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	EVoxelLandChaosCollisionDetail FixedDetail = EVoxelLandChaosCollisionDetail::LOD0;

	// Index is the published visual LOD. A missing visual key uses the unresolved rule.
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled && bMapByRenderLod", EditConditionHides))
	TArray<EVoxelLandChaosCollisionDetail> RenderLodDetails = {EVoxelLandChaosCollisionDetail::LOD0,
		EVoxelLandChaosCollisionDetail::Disabled, EVoxelLandChaosCollisionDetail::Disabled,
		EVoxelLandChaosCollisionDetail::Disabled, EVoxelLandChaosCollisionDetail::Disabled,
		EVoxelLandChaosCollisionDetail::Disabled};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled && bMapByRenderLod", EditConditionHides))
	EVoxelLandChaosCollisionDetail UnresolvedRenderDetail = EVoxelLandChaosCollisionDetail::LOD0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	TEnumAsByte<ECollisionEnabled::Type> CollisionMode = ECollisionEnabled::QueryAndPhysics;

	// An empty profile uses built-in static blocking responses. Names belong to the host project.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	FName CollisionProfile;

	// An empty value inherits the profile's object type or WorldStatic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	FName ObjectType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "bEnabled", EditConditionHides))
	TArray<FVoxelLandChaosCollisionResponse> Responses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", ClampMax = "512"))
	int32 MaxLeasedTiles = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", ClampMax = "512"))
	int32 MaxQueuedMeshes = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", ClampMax = "16"))
	int32 MaxMeshWorkers = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", ClampMax = "16"))
	int32 MaxConcurrentCooks = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", Units = "B"))
	int64 MaxQueuedMeshBytes = 16 * 1024 * 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", Units = "B"))
	int64 MaxPendingCookBytes = 32 * 1024 * 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", Units = "B"))
	int64 MaxCookedBytes = 64 * 1024 * 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "1", ClampMax = "64"))
	int32 MaxPhysicsUpdatesPerTick = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "0.01", ClampMax = "16", Units = "ms"))
	double GameThreadBudgetMs = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision|Budget",
		meta = (EditCondition = "bEnabled", EditConditionHides, ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	double RetirementGraceSeconds = 1.0;

	// Validates the persisted policy independently of world availability and project profile names.
	[[nodiscard]] bool Validate(FString& OutError) const;

	[[nodiscard]] EVoxelLandChaosCollisionDetail ResolveDetail(
		int32 RenderLod = INDEX_NONE, const FVoxelLandChaosCollisionOverride* Override = nullptr) const;

	[[nodiscard]] static int32 GetLod(EVoxelLandChaosCollisionDetail Detail);

private:
	[[nodiscard]] bool ValidateBudget(FString& OutError) const;
	[[nodiscard]] bool ValidateResponses(FString& OutError) const;
};
