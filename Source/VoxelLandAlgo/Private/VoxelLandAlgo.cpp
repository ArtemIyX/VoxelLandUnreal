#include "VoxelLandAlgo.h"
#include "VoxelLandNativeNoise.h"
#include "Operators/VoxelLandSquareRegistration.h"
#include "Operators/VoxelLandExampleFilterRegistration.h"

void FVoxelLandAlgoModule::StartupModule()
{
	VoxelLandNative::Startup();
	FString Error;
    if (!RegisterVoxelLandExampleFilter(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelLand filter provider unavailable: %s"), *Error);
    }
	if (!RegisterVoxelLandSquareOperator(Error))
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelLand square provider unavailable: %s"), *Error);
	}
}

void FVoxelLandAlgoModule::ShutdownModule()
{
	FString Error;
    if (!UnregisterVoxelLandExampleFilter(Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelLand filter provider remains pinned: %s"), *Error);
    }
	if (!UnregisterVoxelLandSquareOperator(Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand square provider remains pinned: %s"), *Error);
	}
	VoxelLandNative::Shutdown();
}

IMPLEMENT_MODULE(FVoxelLandAlgoModule, VoxelLandAlgo)
