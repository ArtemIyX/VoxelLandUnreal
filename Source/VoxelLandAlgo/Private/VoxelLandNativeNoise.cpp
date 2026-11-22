#include "VoxelLandNativeNoise.h"
#include "VoxelLandNativeWorker.h"

namespace VoxelLandNative
{
void Startup() {}
void Shutdown() {}

TUniquePtr<FWorker> FWorker::Create(EBackend Requested, const TArray<FConfig>& Configs, FString& OutError)
{
	OutError.Reset();
	if (IsInGameThread())
	{
		OutError = TEXT("InvalidNoiseInput: native workers must be created off the game thread");
		return nullptr;
	}
	if (Requested != EBackend::Auto && Requested != EBackend::SSE2)
	{
		OutError = TEXT("BackendUnavailable: scalar native noise is not supported");
		return nullptr;
	}
#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
	TUniquePtr<FNativeWorker> Worker = MakeUnique<FNativeWorker>(EBackend::SSE2);
	if (!Worker->Initialize(Configs, OutError))
		return nullptr;
	return Worker;
#else
	OutError = TEXT("BackendUnavailable: native SSE2 noise requires x86-64");
	return nullptr;
#endif
}
}