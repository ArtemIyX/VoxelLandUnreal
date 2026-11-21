#pragma once

#include "CoreMinimal.h"

// Experimental M4 reference parameters. Seed derivation and PRNG are deliberately
// outside this record until the M4 random-stream contract is frozen.
struct FVoxelLandM4ErosionSettings
{
	int32 MaxSteps = 64;
	int32 ThermalIterations = 8;
	int64 CapacityMaxQ8 = 512000000;
	int64 TalusQ8 = 0;
	int64 MaxTransferQ8 = 512000000;
};
