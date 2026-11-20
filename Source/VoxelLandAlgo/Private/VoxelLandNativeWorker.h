#pragma once

#include "VoxelLandNativeNoise.h"
#include "FastSIMD/FastSIMD.h"

namespace VoxelLandNative
{
struct FNode;

// Native handles remain private to this worker and its owning thread.
class FNativeWorker final : public FWorker
{
public:
	explicit FNativeWorker(EBackend InBackend);
	~FNativeWorker() override;
	bool Initialize(FastSIMD::eLevel Level, const TArray<FConfig>& Configs, FString& OutError);
	EBackend GetBackend() const override;
	bool Sample(int32 ConfigIndex, bool b3D, int32 Seed, int32 Count, const float* X, const float* Y, const float* Z,
		float* Output, FString& OutError) override;

private:
	bool InitializeNode(FastSIMD::eLevel Level, const FConfig& Config, int32 Index, FString& OutError);
	bool InitializeFbm(
		FastSIMD::eLevel Level, const FConfig& Config, int32 Index, FNode& Node, double& MaxScale, FString& OutError);
	EBackend Backend;
	uint32 ThreadId;
	TArray<TUniquePtr<FNode>> Nodes;
};
}
