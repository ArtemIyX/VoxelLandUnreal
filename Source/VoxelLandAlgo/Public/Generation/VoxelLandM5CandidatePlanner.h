#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

// Generates bounded, deterministic scratch geometry only. This is neither a
// validated cave plan nor permission to publish terrain. Entrances, masks,
// final-field clearance, surface/roof, water and portal handshakes remain
// mandatory downstream checks. Failure or cancellation leaves Out unchanged.
namespace VoxelLand::M5::CandidatePlanner
{
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TFunctionRef<bool()> IsCancelled, FVoxelLandM5CavePlan& Out);
}
