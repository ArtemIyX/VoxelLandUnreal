#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "VoxelLandMaterialGraphInput.h"

struct FVoxelLandMaterialGraphInstruction
{
	EVoxelLandBuiltinNode Kind = EVoxelLandBuiltinNode::FloatConstant;
	TArray<FVoxelLandMaterialGraphInput, TInlineAllocator<6>> Inputs;
	FGuid NodeId;
	FGuid SignalId;
	FName Channel;
	uint32 SeedSalt = 0;
	uint8 Slot = 0;
};
