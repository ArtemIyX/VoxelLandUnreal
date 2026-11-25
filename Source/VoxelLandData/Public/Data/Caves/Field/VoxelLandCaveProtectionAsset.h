#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Engine/DataAsset.h"
#include "VoxelLandCaveProtectionAsset.generated.h"

// Authored binary protection cells. A set cell protects its whole half-open box.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandM5ProtectionAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Protection", meta = (ToolTip = "Number of cells on each axis."))
	FIntVector CellCounts = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, Category = "Protection", meta = (ToolTip = "Minimum corner in logical world centimetres."))
	FVoxelLandM5PointCm OriginCm;

	UPROPERTY(EditAnywhere, Category = "Protection", meta = (ToolTip = "Positive cell edge length in centimetres."))
	int64 SpacingCm = 0;

	UPROPERTY(EditAnywhere, Category = "Protection",
		meta = (ToolTip = "X-fastest binary cells; one protects the complete cell."))
	TArray<uint8> Cells;
};
