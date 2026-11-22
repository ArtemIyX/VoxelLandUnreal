#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "Data/Structs/VoxelLandM4HeightStageManifest.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "VoxelLandWorldCommonSnapshot.generated.h"

// UObject-free game-thread capture. Program, capability and provider validation
// still precede generation; this record is not a cooked-world identity.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandWorldCommonSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Continental;
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Mountain;
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Peaks;
	UPROPERTY()
	FVoxelLandM4HeightStageManifest HeightStage;
	UPROPERTY()
	int64 Seed = 1234;
	UPROPERTY()
	int32 VoxelSizeCm = 100;
	UPROPERTY()
	int32 BaseHeightQ8 = 0;
	UPROPERTY()
	bool bNormalizeSlope = false;
	UPROPERTY()
	bool bSmoothEnabled = false;
	UPROPERTY()
	float SmoothStrength = 0.5f;
	UPROPERTY()
	FVoxelLandM4HeightDetailConfig HeightDetail;
	UPROPERTY()
	bool bOverhangEnabled = false;
	UPROPERTY()
	int32 OverhangAmplitudeQ8 = 0;
	UPROPERTY()
	int32 OverhangBandQ8 = 256000;
	UPROPERTY()
	float OverhangFrequencyPerCm = 0.001f;
	UPROPERTY()
	FGuid OverhangNodeId;
	UPROPERTY()
	int32 OverhangSalt = 0;
	UPROPERTY()
	FSoftObjectPath MaterialSetupPath;
	UPROPERTY()
	int32 HorizontalRadius = 1;
	UPROPERTY()
	int32 VerticalRadius = 1;
	UPROPERTY()
	FVoxelLandM5CaveSettings Caves;
};
