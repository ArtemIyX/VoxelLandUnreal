#include "VoxelLandNativeNoise.h"

#include "FastNoise/FastNoise.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

#include <limits>

namespace VoxelLandNative
{
namespace
{
// FastNoise's custom SmartNode manager does not document concurrent lifecycle
// operations. Evaluation is worker-exclusive and does not acquire this lock.
FCriticalSection NodeLifecycleMutex;
void* LibraryHandle = nullptr;
FString LibraryError = TEXT("NativeLibraryUnavailable: VoxelLandAlgo has not been started");

struct FNode
{
	FastNoise::SmartNode<FastNoise::Perlin> Perlin;
	FastNoise::SmartNode<FastNoise::FractalFBm> Fbm;
	double MaximumPosition = 0.0;
};

class FNativeWorker final : public FWorker
{
public:
	explicit FNativeWorker(EBackend InBackend)
		: Backend(InBackend)
		, ThreadId(FPlatformTLS::GetCurrentThreadId())
	{
	}

	~FNativeWorker() override
	{
		checkf(ThreadId == FPlatformTLS::GetCurrentThreadId(),
			TEXT("Native noise worker destroyed on a different thread"));
		FScopeLock Lock(&NodeLifecycleMutex);
		Nodes.Empty();
	}

	bool Initialize(FastSIMD::eLevel Level, const TArray<FConfig>& Configs, FString& OutError)
	{
		FScopeLock Lock(&NodeLifecycleMutex);
		Nodes.Reserve(Configs.Num());
		for (int32 Index = 0; Index < Configs.Num(); ++Index)
		{
			const FConfig& Config = Configs[Index];
			if (Config.bFbm
				&& (Config.Octaves < 1 || Config.Octaves > 16 || !FMath::IsFinite(Config.Gain) || Config.Gain < 0.0f
					|| Config.Gain > 1.0f || !FMath::IsFinite(Config.Lacunarity) || Config.Lacunarity < 1.0f))
			{
				OutError = FString::Printf(TEXT("InvalidNoiseInput: invalid fBm configuration %d"), Index);
				return false;
			}

			TUniquePtr<FNode> Node = MakeUnique<FNode>();
			Node->Perlin = FastNoise::New<FastNoise::Perlin>(Level);
			if (!Node->Perlin || Node->Perlin->GetSIMDLevel() != Level)
			{
				OutError = FString::Printf(
					TEXT("BackendUnavailable: Perlin configuration %d did not create the requested level %u"), Index,
					uint32(Level));
				return false;
			}
			double MaxScale = 1.0;
			if (Config.bFbm)
			{
				Node->Fbm = FastNoise::New<FastNoise::FractalFBm>(Level);
				if (!Node->Fbm || Node->Fbm->GetSIMDLevel() != Level)
				{
					OutError = FString::Printf(
						TEXT("BackendUnavailable: fBm configuration %d did not create the requested level %u"), Index,
						uint32(Level));
					return false;
				}
				Node->Fbm->SetSource(Node->Perlin);
				Node->Fbm->SetOctaveCount(Config.Octaves);
				Node->Fbm->SetGain(Config.Gain);
				Node->Fbm->SetLacunarity(Config.Lacunarity);
				for (int32 Octave = 1; Octave < Config.Octaves; ++Octave)
				{
					MaxScale *= double(Config.Lacunarity);
					if (!FMath::IsFinite(MaxScale) || MaxScale > double(std::numeric_limits<float>::max()))
					{
						OutError = FString::Printf(
							TEXT("InvalidNoiseInput: fBm configuration %d exceeds the finite octave scale"), Index);
						return false;
					}
				}
			}
			// Keep every octave well within the vendor's signed lattice range.
			Node->MaximumPosition = 100000000.0 / MaxScale;
			Nodes.Add(MoveTemp(Node));
		}
		return true;
	}

	EBackend GetBackend() const override { return Backend; }

	bool Sample(int32 ConfigIndex, bool b3D, int32 Seed, int32 Count, const float* X, const float* Y, const float* Z,
		float* Output, FString& OutError) override
	{
		if (ThreadId != FPlatformTLS::GetCurrentThreadId() || IsInGameThread())
		{
			OutError = TEXT("InvalidNoiseInput: native worker used on the wrong thread");
			return false;
		}
		if (!Nodes.IsValidIndex(ConfigIndex) || Count < 0 || Count > 256 || !X || !Y || (b3D && !Z) || !Output)
		{
			OutError = TEXT("InvalidNoiseInput: invalid native noise batch");
			return false;
		}
		const FNode& Node = *Nodes[ConfigIndex];
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!FMath::IsFinite(X[Index]) || !FMath::IsFinite(Y[Index])
				|| FMath::Abs(double(X[Index])) > Node.MaximumPosition
				|| FMath::Abs(double(Y[Index])) > Node.MaximumPosition
				|| (b3D && (!FMath::IsFinite(Z[Index]) || FMath::Abs(double(Z[Index])) > Node.MaximumPosition)))
			{
				OutError =
					FString::Printf(TEXT("InvalidNoiseInput: position %d exceeds the safe lattice domain"), Index);
				return false;
			}
		}
		const FastNoise::Generator* Generator = Node.Fbm ? static_cast<const FastNoise::Generator*>(Node.Fbm.get())
														 : static_cast<const FastNoise::Generator*>(Node.Perlin.get());
		if (Count > 0)
		{
			if (b3D)
				Generator->GenPositionArray3D(Output, Count, X, Y, Z, 0.0f, 0.0f, 0.0f, Seed);
			else
				Generator->GenPositionArray2D(Output, Count, X, Y, 0.0f, 0.0f, Seed);
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!FMath::IsFinite(Output[Index]))
			{
				OutError = FString::Printf(TEXT("InvalidNoiseInput: non-finite noise result at sample %d"), Index);
				return false;
			}
		}
		return true;
	}

private:
	EBackend Backend;
	uint32 ThreadId;
	TArray<TUniquePtr<FNode>> Nodes;
};
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
