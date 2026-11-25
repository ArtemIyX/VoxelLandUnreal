#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Field/Execution/VoxelLandVMBackend.h"
#include "Data/Field/Execution/VoxelLandNoiseBackend.h"

class FVoxelLandGenerator;

namespace VoxelLandM1Fixtures
{
struct FResult
{
	bool bOffGameThread = false;
	FVoxelLandError Error;
	EVoxelLandVMBackend VM = EVoxelLandVMBackend::Auto;
	EVoxelLandNoiseBackend NoiseBackend = EVoxelLandNoiseBackend::Auto;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
};
}
