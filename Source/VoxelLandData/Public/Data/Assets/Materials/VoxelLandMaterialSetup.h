#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelLandMaterialSetup.generated.h"

class UMaterialInterface;

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
};
