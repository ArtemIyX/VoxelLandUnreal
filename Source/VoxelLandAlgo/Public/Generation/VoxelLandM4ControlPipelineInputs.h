#pragma once

#include "Generation/VoxelLandM4ControlChannel.h"
#include "CoreTypes.h"

struct FGuid;
struct FVoxelLandM4DomainSettings;
struct FVoxelLandM4ErosionSettings;

struct FVoxelLandM4ControlPipelineInputs
{
	const FVoxelLandM4DomainSettings& Domain;
	FVoxelLandM4ControlChannel Continental;
	FVoxelLandM4ControlChannel Mountain;
	FVoxelLandM4ControlChannel Peaks;
	int32 BaseQ8;
	uint64 WorldSeed;
	const FGuid& StageId;
	uint32 Salt;
	int32 DropletCount;
	const FVoxelLandM4ErosionSettings& Erosion;
};
