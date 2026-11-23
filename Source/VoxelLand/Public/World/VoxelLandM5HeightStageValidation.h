#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandM4WorldSnapshot;

// Bounded semantic projection comparison. Snapshot data must remain immutable
// throughout. Authoring-only parameter Tooltip is excluded and never read;
// other parameter, node, pin, link and function fields are compared. Strings
// containing embedded nulls are rejected. Success does not validate programs,
// providers or world identity.
class VOXELLAND_API FVoxelLandM5HeightStageValidation final
{
public:
	static FVoxelLandM5Diagnostic Validate(const FVoxelLandM4WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
};
