#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Curves/VoxelLandCurveAuthoringKey.h"
#include "Data/Terrain/Curves/VoxelLandCookedCurve.h"
#include "Data/Graph/Sources/VoxelLandParameterOverride.h"
#include "VoxelLandControlConfig.generated.h"

class UVoxelLandTerrainGraph;

// One independently seeded, single-output 2D control graph and its height curve.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandM4ControlConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Control")
	TObjectPtr<UVoxelLandTerrainGraph> Graph;

	UPROPERTY(EditAnywhere, Category = "Control")
	TArray<FVoxelLandParameterOverride> Overrides;

	// Strictly increasing inputs, including 0 and 65535. Output is Q8 centimeters.
	UPROPERTY(EditAnywhere, Category = "Control")
	TArray<FVoxelLandM4CurveAuthoringKey> CurveKeys;

	// Stored on save/cook, never rebuilt by runtime workers.
	UPROPERTY()
	FVoxelLandM4CookedCurve CookedCurve;
};
