#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Execution/VoxelLandBackendOptions.h"
#include "Data/TerrainOverlay/Filters/VoxelLandTerrainFilterSettings.h"
#include "Data/TerrainOverlay/Redistance/VoxelLandTerrainRedistanceSettings.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainSphereSettings.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainStampSettings.h"
#include "Data/World/Configuration/VoxelLandDensityBounds.h"
#include "Data/World/Configuration/VoxelLandZoneProfile.h"

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
	TOptional<FVoxelLandDensityBounds> DirectDensityBounds;
};
