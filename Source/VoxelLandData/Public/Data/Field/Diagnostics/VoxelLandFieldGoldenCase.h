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
struct FGolden
{
	int32 Size;
	FIntVector Key;
};
}
