#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4ControlConfig.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "VoxelLandWorldCommonSettings.generated.h"

class UVoxelLandMaterialSetup;

// Shared authoring inputs. Graph assets are captured on the game thread.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandWorldCommonSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (ToolTip = "Continental control graph and height curve."))
	FVoxelLandM4ControlConfig Continental;
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (ToolTip = "Mountain control graph and height curve."))
	FVoxelLandM4ControlConfig Mountain;
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (ToolTip = "Peaks control graph and height curve."))
	FVoxelLandM4ControlConfig Peaks;
	UPROPERTY(EditAnywhere, Category = "Generation", meta = (ToolTip = "World generation seed."))
	int64 Seed = 1234;
	UPROPERTY(EditAnywhere, Category = "Generation",
		meta = (ClampMin = "1", ClampMax = "134217", ToolTip = "Voxel cell width in centimetres."))
	int32 VoxelSizeCm = 100;
	UPROPERTY(EditAnywhere, Category = "Generation", meta = (ToolTip = "Base surface height in Q8 centimetres."))
	int32 BaseHeightQ8 = 0;
	UPROPERTY(
		EditAnywhere, Category = "Generation", meta = (ToolTip = "Normalize the height field by its local slope."))
	bool bNormalizeSlope = false;
	UPROPERTY(EditAnywhere, Category = "Smooth", meta = (ToolTip = "Apply the bounded one-cell density filter."))
	bool bSmoothEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Smooth",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bSmoothEnabled",
			ToolTip = "Density filter strength."))
	float SmoothStrength = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Height Detail", meta = (ToolTip = "Optional near-surface height detail."))
	FVoxelLandM4HeightDetailConfig HeightDetail;
	UPROPERTY(
		EditAnywhere, Category = "Overhang", meta = (ToolTip = "Enable bounded near-surface overhang displacement."))
	bool bOverhangEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Overhang",
		meta = (ClampMin = "0", ClampMax = "128000", EditCondition = "bOverhangEnabled",
			ToolTip = "Maximum overhang displacement in Q8 centimetres."))
	int32 OverhangAmplitudeQ8 = 0;
	UPROPERTY(EditAnywhere, Category = "Overhang",
		meta = (ClampMin = "1", ClampMax = "1280000", EditCondition = "bOverhangEnabled",
			ToolTip = "Overhang surface band in Q8 centimetres."))
	int32 OverhangBandQ8 = 256000;
	UPROPERTY(EditAnywhere, Category = "Overhang",
		meta = (ClampMin = "0.000001", ClampMax = "1.0", EditCondition = "bOverhangEnabled",
			ToolTip = "Overhang frequency per centimetre."))
	float OverhangFrequencyPerCm = 0.001f;
	UPROPERTY(EditAnywhere, Category = "Overhang",
		meta = (EditCondition = "bOverhangEnabled", ToolTip = "Stable identifier for the overhang random stream."))
	FGuid OverhangNodeId = FGuid(0x564c4d34, 0x4f564552, 1, 1);
	UPROPERTY(EditAnywhere, Category = "Overhang",
		meta = (ClampMin = "0", EditCondition = "bOverhangEnabled", ToolTip = "Independent overhang seed salt."))
	int32 OverhangSalt = 0;
	UPROPERTY(EditAnywhere, Category = "Rendering", meta = (ToolTip = "Optional material setup for rendered terrain."))
	TSoftObjectPtr<UVoxelLandMaterialSetup> MaterialSetup;
	UPROPERTY(EditAnywhere, Category = "Streaming",
		meta = (ClampMin = "1", ClampMax = "8", ToolTip = "Horizontal startup chunk radius."))
	int32 HorizontalRadius = 1;
	UPROPERTY(EditAnywhere, Category = "Streaming",
		meta = (ClampMin = "1", ClampMax = "4", ToolTip = "Vertical startup chunk radius."))
	int32 VerticalRadius = 1;
	// Retained for worker capture, hidden until the cave pipeline is implemented.
	UPROPERTY()
	FVoxelLandM5CaveSettings Caves;
};
