#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandM4WorldSnapshot;

// Bounded wrapper around the existing M4 projection comparison. Snapshot data
// must remain immutable throughout. Version-one projections containing root or
// function parameters fail Unproven: arbitrary FText histories cannot yet be
// bounded. Success does not validate programs, providers or world identity.
class VOXELLAND_API FVoxelLandM5HeightStageValidation final
{
public:
	static FVoxelLandM5Diagnostic Validate(const FVoxelLandM4WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
};
