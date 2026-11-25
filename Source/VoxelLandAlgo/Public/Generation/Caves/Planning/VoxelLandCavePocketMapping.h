#pragma once

#include "CoreMinimal.h"

// Converts one dimensionless pocket-noise sample into an optional Q8 cm carve
// contribution. This is not an exact SDF and does not enforce depth, roof,
// protection, water, attachment or final-field clearance rules.
namespace VoxelLand::M5::PocketMapping
{
// Sample is in [-32768, 32767], ThresholdQ16 in [0, 65535], and AmplitudeCm
// in [0, 1048576]. A sample is selected only if it is strictly above the
// threshold after subtracting 32768. Selected samples produce negative C;
// non-selected, disabled and zero-amplitude samples have no contribution.
// Both outputs remain unchanged on invalid input.
VOXELLANDALGO_API bool TryMap(bool bEnabled, int32 Sample, int32 ThresholdQ16, int32 AmplitudeCm,
	bool& bOutHasContribution, int64& OutDistanceQ8);
}
