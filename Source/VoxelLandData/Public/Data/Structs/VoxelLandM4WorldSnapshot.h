#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandM4DomainSettings.h"
#include "Data/Structs/VoxelLandM4ErosionSettings.h"
#include "Data/Structs/VoxelLandM4SmoothSettings.h"

// Plain immutable-by-convention worker input. Source graph identities are checked
// by the caller against the captured source and payload using the program codec.
struct FVoxelLandM4WorldSnapshot
{
	int32 FormatVersion = 1;
	FVoxelLandM4ControlSnapshot Continental;
	FVoxelLandM4ControlSnapshot Mountain;
	FVoxelLandM4ControlSnapshot Peaks;
	FVoxelLandM4DomainSettings Domain;
	FVoxelLandM4ErosionSettings Erosion;
	int64 Seed = 1234;
	int32 VoxelSizeCm = 100;
	int32 BaseHeightQ8 = 0;
	FGuid ErosionStageId;
	int32 ErosionSalt = 0;
	int32 DropletCount = 4096;
	bool bNormalizeSlope = false;
	FVoxelLandM4SmoothSettings Smooth;
	bool bOverhangEnabled = false;
	int32 OverhangAmplitudeQ8 = 0;
	int32 OverhangBandQ8 = 256000;
	float OverhangFrequencyPerCm = 0.001f;
	FGuid OverhangNodeId;
	int32 OverhangSalt = 0;
	int32 HorizontalRadius = 1;
	int32 VerticalRadius = 1;
	FSoftObjectPath MaterialSetupPath;
};
