#include "VoxelLandSquareRegistration.h"
#include "Operators/VoxelLandOperator.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Math/VectorRegister.h"

#ifndef VOXELLAND_ENABLE_VM_SIMD
#define VOXELLAND_ENABLE_VM_SIMD 1
#endif
#if VOXELLAND_ENABLE_VM_SIMD && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS && PLATFORM_ENABLE_VECTORINTRINSICS
#define VOXELLAND_SQUARE_HAS_SSE2 1
#else
#define VOXELLAND_SQUARE_HAS_SSE2 0
#endif

namespace
{
class FSquareWorker final : public IVoxelLandOperatorWorker
{
public:
	bool Evaluate(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count,
		FString& Error) override
	{
		if (Inputs.Num() != 1 || !Inputs[0] || Output.Num() != Count || Count < 1 || Count > 256)
		{
			Error = TEXT("Invalid square batch");
			return false;
		}
		const float* Input = static_cast<const float*>(Inputs[0]);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Result = Input[Index] * Input[Index];
			if (!Output.WriteFloat(Index, Result))
			{
				Error = TEXT("Square result is non-finite");
				return false;
			}
		}
		return true;
	}
#if VOXELLAND_SQUARE_HAS_SSE2
	bool EvaluateSimd(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count,
		FString& Error) override
	{
		if (Inputs.Num() != 1 || !Inputs[0] || Output.Num() != Count || Count < 4 || Count > 256 || Count % 4 != 0)
		{
			Error = TEXT("Invalid square SIMD batch");
			return false;
		}
		const float* Input = static_cast<const float*>(Inputs[0]);
		for (int32 Index = 0; Index < Count; Index += 4)
		{
			const VectorRegister4Float Value = VectorLoad(Input + Index);
			float Results[4];
			VectorStore(VectorMultiply(Value, Value), Results);
			if (!Output.WriteFloat4(Index, Results))
			{
				Error = TEXT("Square SIMD result is invalid");
				return false;
			}
		}
		return true;
	}
#endif
};

class FSquareOperator final : public IVoxelLandOperator
{
public:
	FGuid Id() const override { return VoxelLandSquareOperatorId(); }
	int32 Version() const override { return 1; }
	TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
	EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
	uint32 MaximumWorkerBytes() const override { return sizeof(FSquareWorker); }

	EVoxelLandOperatorSimdRequirement SimdRequirement() const override
	{
#if VOXELLAND_SQUARE_HAS_SSE2
		return EVoxelLandOperatorSimdRequirement::SSE2;
#else
		return EVoxelLandOperatorSimdRequirement::None;
#endif
	}

	bool ValidateConfiguration(TConstArrayView<uint8> Config, FString& Error) const override
	{
		if (!Config.IsEmpty())
		{
			Error = TEXT("Square takes no configuration");
			return false;
		}
		return true;
	}

	TUniquePtr<IVoxelLandOperatorWorker>
	CreateWorker(TConstArrayView<uint8> Config, uint64 Seed, FString& Error) const override
	{
		if (!ValidateConfiguration(Config, Error))
		{
			return nullptr;
		}
		return MakeUnique<FSquareWorker>();
	}
};
}

bool RegisterVoxelLandSquareOperator(FString& Error)
{
	return FVoxelLandOperatorRegistry::RegisterV2(FName(TEXT("VoxelLandAlgo")),
		MakeShared<FSquareOperator, ESPMode::ThreadSafe>(), FVoxelLandOperatorRegistry::ProviderAbiVersion, Error);
}

bool UnregisterVoxelLandSquareOperator(FString& Error)
{
	return FVoxelLandOperatorRegistry::Unregister(VoxelLandSquareOperatorId(), 1, Error);
}