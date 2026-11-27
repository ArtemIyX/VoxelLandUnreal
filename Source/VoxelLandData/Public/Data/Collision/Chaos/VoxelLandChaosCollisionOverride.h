#pragma once

#include "Data/Collision/Chaos/VoxelLandChaosCollisionDetail.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverrideMode.h"
#include "VoxelLandChaosCollisionOverride.generated.h"

USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandChaosCollisionOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision")
	EVoxelLandChaosCollisionOverrideMode Mode = EVoxelLandChaosCollisionOverrideMode::Inherit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "Mode == EVoxelLandChaosCollisionOverrideMode::Enabled", EditConditionHides))
	bool bOverrideDetail = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision",
		meta = (EditCondition = "Mode == EVoxelLandChaosCollisionOverrideMode::Enabled && bOverrideDetail",
			EditConditionHides))
	EVoxelLandChaosCollisionDetail Detail = EVoxelLandChaosCollisionDetail::LOD0;

	[[nodiscard]] bool IsValid() const;
	friend bool operator==(const FVoxelLandChaosCollisionOverride&, const FVoxelLandChaosCollisionOverride&) = default;
};
