#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

// Pure graph validation; success is not a geometry or walkability certificate.
namespace VoxelLand::M5::Topology
{
// Count limits are checked before scratch allocation, with hard ceilings to
// bound work even if a caller passes untrusted admission limits. A diagnostic
// is returned by value; no plan or caller-owned output is modified.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Validate(const FVoxelLandM5CavePlan& Plan, int64 ExpectedWorldSeed,
	const FGuid& ExpectedStageId, int64 ExpectedSalt, int32 MaxNodes, int32 MaxConnections,
	TFunctionRef<bool()> IsCancelled);
}
