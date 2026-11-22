#pragma once

#include "CoreMinimal.h"
#include "Generation/VoxelLandM5FeetProposal.h"

// Uncertified feet motion associated with one plan-local segment ID.
struct VOXELLANDALGO_API FVoxelLandM5FeetChainLeg
{
	uint64 SegmentId = 0;
	FVoxelLandM5FeetProposal Feet;
};
