#pragma once

#include "CoreMinimal.h"

// Private implementation interface: native FastNoise headers never escape VoxelLandAlgo.
namespace VoxelLandNative
{
enum class EBackend : uint8
{
	Auto,
	Scalar,
	SSE2
};

struct FConfig
{
	bool bFbm = false;
	int32 Octaves = 3;
	float Gain = 0.5f;
	float Lacunarity = 2.0f;
};

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
