#include "VoxelLandNativeWorker.h"

#include "FastNoise/FastNoise.h"
#include "HAL/PlatformTLS.h"
#include "Misc/ScopeLock.h"

#include <limits>

namespace VoxelLandNative
{
namespace
{
// FastNoise's SmartNode manager does not document concurrent lifecycle operations.
FCriticalSection NodeLifecycleMutex;
}

struct FNode
{
	FastNoise::SmartNode<FastNoise::Perlin> Perlin;
	FastNoise::SmartNode<FastNoise::FractalFBm> Fbm;
	double MaximumPosition = 0.0;
};

FNativeWorker::FNativeWorker(EBackend InBackend)
	: Backend(InBackend)
	, ThreadId(FPlatformTLS::GetCurrentThreadId())
{
}

FNativeWorker::~FNativeWorker()
{
	checkf(ThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("Native noise worker destroyed on a different thread"));
	FScopeLock Lock(&NodeLifecycleMutex);
	Nodes.Empty();
}

bool FNativeWorker::Initialize(FastSIMD::eLevel Level, const TArray<FConfig>& Configs, FString& OutError)
{
	FScopeLock Lock(&NodeLifecycleMutex);
	Nodes.Reserve(Configs.Num());
	for (int32 Index = 0; Index < Configs.Num(); ++Index)
	{
		if (!InitializeNode(Level, Configs[Index], Index, OutError))
			return false;
	}
	return true;
}

bool FNativeWorker::InitializeNode(FastSIMD::eLevel Level, const FConfig& Config, int32 Index, FString& OutError)
{
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
		OutError =
			FString::Printf(TEXT("BackendUnavailable: Perlin configuration %d did not create the requested level %u"),
				Index, uint32(Level));
		return false;
	}
	double MaxScale = 1.0;
	if (Config.bFbm && !InitializeFbm(Level, Config, Index, *Node, MaxScale, OutError))
		return false;
	// Keep every octave well within the vendor's signed lattice range.
	Node->MaximumPosition = 100000000.0 / MaxScale;
	Nodes.Add(MoveTemp(Node));
	return true;
}

bool FNativeWorker::InitializeFbm(
	FastSIMD::eLevel Level, const FConfig& Config, int32 Index, FNode& Node, double& MaxScale, FString& OutError)
{
	Node.Fbm = FastNoise::New<FastNoise::FractalFBm>(Level);
	if (!Node.Fbm || Node.Fbm->GetSIMDLevel() != Level)
	{
		OutError =
			FString::Printf(TEXT("BackendUnavailable: fBm configuration %d did not create the requested level %u"),
				Index, uint32(Level));
		return false;
	}
	Node.Fbm->SetSource(Node.Perlin);
	Node.Fbm->SetOctaveCount(Config.Octaves);
	Node.Fbm->SetGain(Config.Gain);
	Node.Fbm->SetLacunarity(Config.Lacunarity);
	for (int32 Octave = 1; Octave < Config.Octaves; ++Octave)
	{
		MaxScale *= double(Config.Lacunarity);
		if (!FMath::IsFinite(MaxScale) || MaxScale > double(std::numeric_limits<float>::max()))
		{
			OutError =
				FString::Printf(TEXT("InvalidNoiseInput: fBm configuration %d exceeds the finite octave scale"), Index);
			return false;
		}
	}
	return true;
}

EBackend FNativeWorker::GetBackend() const
{
	return Backend;
}

bool FNativeWorker::Sample(int32 ConfigIndex, bool b3D, int32 Seed, int32 Count, const float* X, const float* Y,
	const float* Z, float* Output, FString& OutError)
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
			OutError = FString::Printf(TEXT("InvalidNoiseInput: position %d exceeds the safe lattice domain"), Index);
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
}
