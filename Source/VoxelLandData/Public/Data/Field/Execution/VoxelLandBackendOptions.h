#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Execution/VoxelLandNoiseBackend.h"
#include "Data/Field/Execution/VoxelLandVMBackend.h"

struct FVoxelLandBackendOptions
{
	EVoxelLandVMBackend VM = EVoxelLandVMBackend::Auto;
	EVoxelLandNoiseBackend Noise = EVoxelLandNoiseBackend::Auto;
};
