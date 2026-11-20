#include "VoxelLandNativeNoise.h"
#include "VoxelLandNativeWorker.h"

#include "FastNoise/FastNoise.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace VoxelLandNative
{
namespace
{
void* LibraryHandle = nullptr;
FString LibraryError = TEXT("NativeLibraryUnavailable: VoxelLandAlgo has not been started");
}

void Startup()
{
	LibraryError = TEXT("NativeLibraryUnavailable: UnrealFastNoise2 module failed to load");
	if (!FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("UnrealFastNoise2")))
		return;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealFastNoise2"));
	if (!Plugin.IsValid())
	{
		LibraryError = TEXT("NativeLibraryUnavailable: UnrealFastNoise2 plugin was not found");
		return;
	}
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT(FASTNOISE_LIBRARY_PATH));
	LibraryHandle = FPlatformProcess::GetDllHandle(*Path);
	if (!LibraryHandle)
	{
		LibraryError = FString::Printf(TEXT("NativeLibraryUnavailable: could not load %s"), *Path);
		return;
	}
	// Check stable C exports before attempting any native C++ calls.
	if (!FPlatformProcess::GetDllExport(LibraryHandle, TEXT("fnNewFromEncodedNodeTree"))
		|| !FPlatformProcess::GetDllExport(LibraryHandle, TEXT("fnGetSIMDLevel")))
	{
		LibraryError = FString::Printf(TEXT("NativeLibraryUnavailable: required exports are missing from %s"), *Path);
		FPlatformProcess::FreeDllHandle(LibraryHandle);
		LibraryHandle = nullptr;
		return;
	}
	LibraryError.Reset();
}

void Shutdown()
{
	if (LibraryHandle)
	{
		FPlatformProcess::FreeDllHandle(LibraryHandle);
		LibraryHandle = nullptr;
	}
	LibraryError = TEXT("NativeLibraryUnavailable: VoxelLandAlgo has shut down");
}

TUniquePtr<FWorker> FWorker::Create(EBackend Requested, const TArray<FConfig>& Configs, FString& OutError)
{
	OutError.Reset();
	if (IsInGameThread())
	{
		OutError = TEXT("InvalidNoiseInput: native workers must be created off the game thread");
		return nullptr;
	}
	if (!LibraryHandle)
	{
		OutError = LibraryError;
		return nullptr;
	}
	const FastSIMD::eLevel CpuLevel = FastSIMD::CPUMaxSIMDLevel();
	const bool bSSE2Available =
		(FastSIMD::COMPILED_SIMD_LEVELS & FastNoise::SUPPORTED_SIMD_LEVELS & FastSIMD::Level_SSE2) != 0
#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
		&& CpuLevel >= FastSIMD::Level_SSE2;
#else
		&& false;
#endif
	if (Requested == EBackend::Auto)
		Requested = bSSE2Available ? EBackend::SSE2 : EBackend::Scalar;
	const FastSIMD::eLevel Level = Requested == EBackend::SSE2 ? FastSIMD::Level_SSE2 : FastSIMD::Level_Scalar;
	if ((Requested != EBackend::SSE2 && Requested != EBackend::Scalar)
		|| (Requested == EBackend::SSE2 && !bSSE2Available)
		|| (FastSIMD::COMPILED_SIMD_LEVELS & FastNoise::SUPPORTED_SIMD_LEVELS & Level) == 0)
	{
		OutError = FString::Printf(
			TEXT("BackendUnavailable: native level %u is not available for this CPU and build"), uint32(Level));
		return nullptr;
	}
	TUniquePtr<FNativeWorker> Worker = MakeUnique<FNativeWorker>(Requested);
	if (!Worker->Initialize(Level, Configs, OutError))
		return nullptr;
	return Worker;
}
}
