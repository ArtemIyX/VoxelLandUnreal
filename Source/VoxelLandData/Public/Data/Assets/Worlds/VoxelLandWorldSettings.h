#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Graph/Sources/VoxelLandParameterOverride.h"
#include "VoxelLandWorldSettings.generated.h"

class UVoxelLandMaterialSetup;
class UVoxelLandTerrainGraph;

UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandWorldSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// Validates source settings only; successful validation does not compile or start terrain.
	bool ValidateSettings(FString& OutError) const;

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand")
	int32 FormatVersion = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TObjectPtr<UVoxelLandTerrainGraph> Graph;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	int64 Seed = 1234;

	UPROPERTY(EditAnywhere, Category = "VoxelLand", meta = (ClampMin = "0.001"))
	float VoxelSizeCm = 100.0f;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TArray<FVoxelLandParameterOverride> Overrides;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TSoftObjectPtr<UVoxelLandMaterialSetup> MaterialSetup;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|Bounds", meta = (ClampMin = "1", ClampMax = "8"))
	int32 HorizontalRadius = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|Bounds", meta = (ClampMin = "1", ClampMax = "4"))
	int32 VerticalRadius = 1;
};
