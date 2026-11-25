#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM6Stamp.h"

class VOXELLANDALGO_API FVoxelLandM6StampComposition final
{
public:
	// The ordinary hard-subtract operation clears the supplied primitive.
	static FVoxelLandM6Stamp CutHole(FVoxelLandM6Stamp Stamp);

	// Append a subtract box followed by an add box. Each box needs a distinct
	// stable ID. Validate the complete ordered result before changing Stamps.
	static FVoxelLandError AppendBoxFoundation(const FVoxelLandM6Stamp& ClearingBox,
		const FVoxelLandM6Stamp& FillingBox, TArray<FVoxelLandM6Stamp>& Stamps);
};
