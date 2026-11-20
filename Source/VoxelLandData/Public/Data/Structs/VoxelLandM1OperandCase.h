#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Enums/VoxelLandOpcode.h"
#include "Data/Enums/VoxelLandVMBackend.h"
#include "Data/Enums/VoxelLandNoiseBackend.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;

namespace VoxelLandM1TestData
{
struct FOperandCase
{
	EVoxelLandOpcode Code;
	int32 Reads;
	uint32 Imm;
};
}
