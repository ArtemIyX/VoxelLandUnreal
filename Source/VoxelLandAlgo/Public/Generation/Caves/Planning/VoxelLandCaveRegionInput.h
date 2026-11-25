#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5CandidateResult;

// Borrowed descriptor bytes must remain immutable for the complete call.
struct VOXELLANDALGO_API FVoxelLandM5RegionInput
{
	FVoxelLandM5RegionCoord Region;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
	TConstArrayView<uint8> Descriptor;
};
