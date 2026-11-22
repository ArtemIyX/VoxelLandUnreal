#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandM4DomainSettings.h"
#include "Data/Structs/VoxelLandM4ErosionSettings.h"
#include "Data/Structs/VoxelLandM4SmoothSettings.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "VoxelLandM4WorldSnapshot.generated.h"

// Plain immutable-by-convention worker input. Source graph identities are checked
// by the caller against the captured source and payload using the program codec.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4WorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	int32 FormatVersion = 1;
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Continental;
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Mountain;
	UPROPERTY()
	FVoxelLandM4ControlSnapshot Peaks;
	UPROPERTY()
	FVoxelLandM4DomainSettings Domain;
	UPROPERTY()
	FVoxelLandM4ErosionSettings Erosion;
	UPROPERTY()
	int64 Seed = 1234;
	UPROPERTY()
	int32 VoxelSizeCm = 100;
	UPROPERTY()
	int32 BaseHeightQ8 = 0;
	UPROPERTY()
	FGuid ErosionStageId;
	UPROPERTY()
	int32 ErosionSalt = 0;
	UPROPERTY()
	int32 DropletCount = 4096;
	UPROPERTY()
	bool bNormalizeSlope = false;
	UPROPERTY()
	FVoxelLandM4SmoothSettings Smooth;
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
	int32 HorizontalRadius = 1;
	UPROPERTY()
	int32 VerticalRadius = 1;
	UPROPERTY()
	FSoftObjectPath MaterialSetupPath;
};
