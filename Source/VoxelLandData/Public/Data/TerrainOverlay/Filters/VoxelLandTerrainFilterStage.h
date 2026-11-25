#pragma once

#include "CoreMinimal.h"
#include "Data/TerrainOverlay/Filters/VoxelLandTerrainFilterKind.h"
#include "VoxelLandTerrainFilterStage.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM6FilterStage
{
	GENERATED_BODY()

	UPROPERTY(
		EditAnywhere, Category = "Filter", meta = (ToolTip = "A unique, stable, non-zero ID within the filter list."))
	FGuid Id;

	UPROPERTY(EditAnywhere, Category = "Filter")
	EVoxelLandM6FilterKind Kind = EVoxelLandM6FilterKind::Smooth;

	UPROPERTY(EditAnywhere, Category = "Filter", meta = (ToolTip = "Inclusive minimum in global logical centimetres."))
	FInt64Vector MinCm = FInt64Vector(0);

	UPROPERTY(EditAnywhere, Category = "Filter", meta = (ToolTip = "Inclusive maximum in global logical centimetres."))
	FInt64Vector MaxCm = FInt64Vector(0);

	UPROPERTY(EditAnywhere, Category = "Filter",
		meta = (EditCondition = "Kind != EVoxelLandM6FilterKind::MakeSpace", EditConditionHides,
			ToolTip = "0 keeps the input, 65535 applies the full filter. Make Space always uses hard strength."))
	uint16 Strength = MAX_uint16;
};
