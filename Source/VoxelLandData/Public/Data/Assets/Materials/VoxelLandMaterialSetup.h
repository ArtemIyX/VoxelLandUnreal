#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/Materials/VoxelLandMaterialRule.h"
#include "Engine/DataAsset.h"
#include "VoxelLandMaterialSetup.generated.h"

class UMaterialInterface;
class UVoxelLandMaterialPalette;

// A soft reference keeps data-only consumers from loading the material and GPU resources.
UCLASS(BlueprintType, HideDropdown)
class VOXELLANDDATA_API UVoxelLandMaterialSetup : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "VoxelLand")
	int32 FormatVersion = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TSoftObjectPtr<UVoxelLandMaterialPalette> Palette;

	UPROPERTY(EditAnywhere, Category = "VoxelLand",
		meta =
			(ToolTip = "Later rules blend their surface over preceding rules. Slot 0 supplies the starting material."))
	TArray<FVoxelLandMaterialRule> Rules;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
};
