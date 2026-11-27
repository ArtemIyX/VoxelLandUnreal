#pragma once

#include "Components/PrimitiveComponent.h"
#include "VoxelLandChaosTerrainComponent.generated.h"

class FVoxelLandChaosCookResult;
class UBodySetup;
class UVoxelLandChaosCookProvider;
struct FVoxelLandChaosCookBodySettings;

UCLASS(Transient, NotBlueprintable)
class VOXELLAND_API UVoxelLandChaosTerrainComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UVoxelLandChaosTerrainComponent();

	// Position is engine-local after the current logical-origin transform.
	[[nodiscard]] static UVoxelLandChaosTerrainComponent* Install(AActor& Owner, const FVector& Position,
		const FVoxelLandChaosCookResult& Cooked, const FVoxelLandChaosCookBodySettings& Settings);

	virtual UBodySetup* GetBodySetup() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

private:
	void Initialize(const FVoxelLandChaosCookResult& Cooked, const FVoxelLandChaosCookBodySettings& Settings);

	UPROPERTY(Transient)
	TObjectPtr<UVoxelLandChaosCookProvider> Provider;

	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> CookedSetup;

	FBox LocalBounds{ForceInit};
	bool bDrawCollision = false;
};
