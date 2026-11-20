#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Structs/VoxelLandZoneProfile.h"

class FVoxelLandProgram;

// Inputs are copied into the immutable generator during Create.
struct FVoxelLandGeneratorSettings
{
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	uint64 WorldSeed = 0;
	FVoxelLandZoneProfile Profile;
	FVoxelLandBackendOptions Backends;
};
