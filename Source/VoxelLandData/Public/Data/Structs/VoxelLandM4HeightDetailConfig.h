#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM4HeightDetailConfig.generated.h"

// Version 1: independently seeded 2D Perlin, quantized to signed Q15.
// Amplitude is a nonnegative Q8 centimeter bound; disabled is an exact no-op.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4HeightDetailConfig
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Height Detail")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Height Detail")
	int32 AlgorithmVersion = 1;
	UPROPERTY(EditAnywhere, Category = "Height Detail", meta = (ClampMin = "0", ClampMax = "1280000"))
	int32 AmplitudeQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Height Detail", meta = (ClampMin = "0.000001", ClampMax = "1"))
	float FrequencyPerCm = 0.001f;
	UPROPERTY(EditAnywhere, Category = "Height Detail")
	FGuid NodeId = FGuid(0x564c4d34, 0x4844544c, 1, 1);
	UPROPERTY(EditAnywhere, Category = "Height Detail")
	uint32 Salt = 0;
};
