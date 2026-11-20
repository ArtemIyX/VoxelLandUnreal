#include "VoxelLandAlgo.h"
#include "VoxelLandNativeNoise.h"

void FVoxelLandAlgoModule::StartupModule()
{
    VoxelLandNative::Startup();
}

void FVoxelLandAlgoModule::ShutdownModule()
{
    VoxelLandNative::Shutdown();
}

IMPLEMENT_MODULE(FVoxelLandAlgoModule, VoxelLandAlgo)
