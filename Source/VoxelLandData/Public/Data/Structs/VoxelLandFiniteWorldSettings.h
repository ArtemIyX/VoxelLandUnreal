#pragma once

#include "CoreMinimal.h"
#include "VoxelLandFiniteWorldSettings.generated.h"

// A bounded control grid and its optional world-wide erosion prepass.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandFiniteWorldSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Domain",
		meta = (ToolTip = "Logical world X at the first control-grid sample, in centimetres."))
	int64 OriginXCm = -819200;
	UPROPERTY(EditAnywhere, Category = "Domain",
		meta = (ToolTip = "Logical world Y at the first control-grid sample, in centimetres."))
	int64 OriginYCm = -819200;
	UPROPERTY(EditAnywhere, Category = "Domain",
		meta = (ClampMin = "1", ClampMax = "1024", ToolTip = "Number of control-grid cells along X."))
	int32 CellsX = 256;
	UPROPERTY(EditAnywhere, Category = "Domain",
		meta = (ClampMin = "1", ClampMax = "1024", ToolTip = "Number of control-grid cells along Y."))
	int32 CellsY = 256;
	UPROPERTY(EditAnywhere, Category = "Domain",
		meta = (ClampMin = "1", ToolTip = "Distance between adjacent control-grid samples in centimetres."))
	int64 GridSpacingCm = 6400;
	UPROPERTY(
		EditAnywhere, Category = "Domain", meta = (ToolTip = "Inclusive lower cave-planning height in centimetres."))
	int64 MinZCm = -819200;
	UPROPERTY(
		EditAnywhere, Category = "Domain", meta = (ToolTip = "Exclusive upper cave-planning height in centimetres."))
	int64 MaxZCm = 819200;
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ToolTip = "Run the finite world's global erosion prepass."))
	bool bGlobalErosion = true;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ToolTip = "Stable identifier used to derive the erosion random stream."))
	FGuid ErosionStageId = FGuid(0x564c4d34, 0x45524f53, 1, 1);
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ToolTip = "Independent salt for the erosion random stream."))
	int32 ErosionSalt = 0;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "65536", ToolTip = "Number of erosion droplets."))
	int32 DropletCount = 4096;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "128", ToolTip = "Maximum steps per erosion droplet."))
	int32 MaxSteps = 64;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "32", ToolTip = "Number of thermal erosion iterations."))
	int32 ThermalIterations = 8;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000", ToolTip = "Maximum sediment capacity in Q8 centimetres."))
	int64 CapacityMaxQ8 = 512000000;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000", ToolTip = "Thermal erosion talus threshold in Q8 centimetres."))
	int64 TalusQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Erosion",
		meta = (ClampMin = "0", ClampMax = "512000000", ToolTip = "Maximum thermal transfer in Q8 centimetres."))
	int64 MaxTransferQ8 = 512000000;
};
