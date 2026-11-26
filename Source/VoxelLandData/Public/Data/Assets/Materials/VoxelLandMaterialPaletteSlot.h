#pragma once

#include "CoreMinimal.h"
#include "VoxelLandMaterialPaletteSlot.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandMaterialPaletteSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Appearance")
	FGuid SurfaceId;

	UPROPERTY(VisibleAnywhere, Category = "Appearance", meta = (Units = "cm"))
	double TilePeriodCm = 512.0;

	UPROPERTY(VisibleAnywhere, Category = "Appearance")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(VisibleAnywhere, Category = "Appearance")
	float HeightBlendContrast = 0.25f;

	[[nodiscard]] bool IsValid() const;
};
