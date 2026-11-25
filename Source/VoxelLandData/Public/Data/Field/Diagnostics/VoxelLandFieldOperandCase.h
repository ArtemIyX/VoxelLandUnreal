#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Program/Execution/VoxelLandOpcode.h"
#include "Data/Field/Execution/VoxelLandVMBackend.h"
#include "Data/Field/Execution/VoxelLandNoiseBackend.h"

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
