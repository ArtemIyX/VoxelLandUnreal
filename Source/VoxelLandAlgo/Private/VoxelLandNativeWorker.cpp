#include "VoxelLandNativeWorker.h"
#include "FastNoiseNative.h"
#include "Data/Structs/VoxelLandNativeNode.h"
#include "HAL/PlatformTLS.h"
#include <limits>

namespace VoxelLandNative
{
FNode::FNode() = default;
FNode::~FNode() = default;

FNativeWorker::FNativeWorker(EBackend InBackend)
	: Backend(InBackend)
	, ThreadId(FPlatformTLS::GetCurrentThreadId())
{
}

FNativeWorker::~FNativeWorker()
{
	checkf(ThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("Native noise worker destroyed on a different thread"));
	Nodes.Empty();
}

bool FNativeWorker::Initialize(const TArray<FConfig>& Configs, FString& OutError)
{
	Nodes.Reserve(Configs.Num());
	for (int32 Index = 0; Index < Configs.Num(); ++Index)
	{
		const FConfig& Config = Configs[Index];
		if (Config.Family == EVoxelLandNoiseFamily::Fbm
			&& (Config.Octaves < 1 || Config.Octaves > 16 || !FMath::IsFinite(Config.Gain) || Config.Gain < 0.f
				|| Config.Gain > 1.f || !FMath::IsFinite(Config.Lacunarity) || Config.Lacunarity < 1.f))
		{
			OutError = FString::Printf(TEXT("InvalidNoiseInput: invalid fBm configuration %d"), Index);
			return false;
		}
		double MaxScale = 1.0;
		if (Config.Family == EVoxelLandNoiseFamily::Fbm)
		{
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
		TUniquePtr<FNode> Node = MakeUnique<FNode>();
		switch (Config.Family)
		{
			case EVoxelLandNoiseFamily::Perlin:
				Node->Generator = FFastNoiseNative::CreatePerlin(OutError, true);
				break;
			case EVoxelLandNoiseFamily::Fbm:
				Node->Generator =
					FFastNoiseNative::CreateFbm(Config.Octaves, Config.Gain, Config.Lacunarity, OutError, true);
				break;
			case EVoxelLandNoiseFamily::Value:
				Node->Generator = FFastNoiseNative::CreateValue(OutError, true);
				break;
			case EVoxelLandNoiseFamily::Simplex:
				Node->Generator = FFastNoiseNative::CreateSimplex(OutError, true);
				break;
			default:
				OutError = TEXT("InvalidNoiseInput: unknown noise family");
				return false;
		}
		if (!Node->Generator)
		{
			return false;
		}
		Node->MaximumPosition = 100000000.0 / MaxScale;
		Nodes.Add(MoveTemp(Node));
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
	const bool bSuccess = b3D ? Node.Generator->Sample3D(Count, X, Y, Z, Seed, Output, OutError)
							  : Node.Generator->Sample2D(Count, X, Y, Seed, Output, OutError);
	if (!bSuccess)
	{
		return false;
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
