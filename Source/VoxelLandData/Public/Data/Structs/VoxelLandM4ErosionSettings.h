#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM4ErosionSettings.generated.h"

// Experimental M4 reference parameters. Seed derivation and PRNG are deliberately
// outside this record until the M4 random-stream contract is frozen.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4ErosionSettings
{
	GENERATED_BODY()
	UPROPERTY()
	int32 MaxSteps = 64;
	UPROPERTY()
	int32 ThermalIterations = 8;
	UPROPERTY()
	int64 CapacityMaxQ8 = 512000000;
	UPROPERTY()
	int64 TalusQ8 = 0;
	UPROPERTY()
	int64 MaxTransferQ8 = 512000000;
};
