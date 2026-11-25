#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Structs/VoxelLandM6FilterSettings.h"
#include "Data/Structs/VoxelLandM6RedistanceSettings.h"
#include "Data/Structs/VoxelLandM6SphereSettings.h"
#include "Data/Structs/VoxelLandM6StampSettings.h"
#include "Data/Structs/VoxelLandZoneProfile.h"

class FVoxelLandProgram;

// Inputs are copied into the immutable generator during Create.
struct FVoxelLandGeneratorSettings
{
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	uint64 WorldSeed = 0;
	FVoxelLandZoneProfile Profile;
	FVoxelLandBackendOptions Backends;
	// Used by fixed-lattice unlimited chunks when shaping an intermediate height grid.
	bool bM4FixedLatticeShaping = false;
	// Transient generation-time inputs. Only one list can be active.
	FVoxelLandM6SphereSettings M6Spheres; // First finite sphere checkpoint API.
	FVoxelLandM6StampSettings M6Stamps;
	FVoxelLandM6FilterSettings M6Filters;
	FVoxelLandM6RedistanceSettings M6Redistance;
};
