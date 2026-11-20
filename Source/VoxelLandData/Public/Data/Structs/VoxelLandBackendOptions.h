#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandNoiseBackend.h"
#include "Data/Enums/VoxelLandVMBackend.h"

struct FVoxelLandBackendOptions
{
	EVoxelLandVMBackend VM = EVoxelLandVMBackend::Auto;
	EVoxelLandNoiseBackend Noise = EVoxelLandNoiseBackend::Auto;
};
