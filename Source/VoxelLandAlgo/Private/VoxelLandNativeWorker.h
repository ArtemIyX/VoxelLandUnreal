#pragma once
#include "VoxelLandNativeNoise.h"
namespace VoxelLandNative
{
struct FNode;
class FNativeWorker final : public FWorker
{
public:
	explicit FNativeWorker(EBackend InBackend);
	~FNativeWorker() override;
	bool Initialize(const TArray<FConfig>& Configs, FString& OutError);
	EBackend GetBackend() const override;
	bool Sample(int32 ConfigIndex, bool b3D, int32 Seed, int32 Count, const float* X, const float* Y, const float* Z,
		float* Output, FString& OutError) override;

private:
	EBackend Backend;
	uint32 ThreadId;
	TArray<TUniquePtr<FNode>> Nodes;
};
}
