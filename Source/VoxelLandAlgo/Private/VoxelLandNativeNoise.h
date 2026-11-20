#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandNativeBackend.h"
#include "Data/Structs/VoxelLandNativeConfig.h"

// Private implementation interface: native FastNoise headers never escape VoxelLandAlgo.
namespace VoxelLandNative
{
// The owner must create, use, and destroy a worker on the same non-game thread.
class FWorker
{
public:
	virtual ~FWorker() = default;
	static TUniquePtr<FWorker> Create(EBackend Backend, const TArray<FConfig>& Configs, FString& OutError);
	virtual EBackend GetBackend() const = 0;
	virtual bool Sample(int32 ConfigIndex, bool b3D, int32 Seed, int32 Count, const float* X, const float* Y,
		const float* Z, float* Output, FString& OutError) = 0;
};

// Called only by module startup/shutdown, never by generator execution.
void Startup();
void Shutdown();
}
