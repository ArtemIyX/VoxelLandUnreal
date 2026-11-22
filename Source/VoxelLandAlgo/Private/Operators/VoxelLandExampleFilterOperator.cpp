#include "Operators/VoxelLandExampleFilterRegistration.h"
#include "Operators/VoxelLandExampleFilterOperator.h"
#include "Operators/VoxelLandOperator.h"

namespace
{
class FExampleFilterWorker final : public IVoxelLandOperatorWorker
{
public:
	bool Evaluate(TConstArrayView<const void*>, FVoxelLandOperatorOutput&, int32, FString& Error) override
	{
		Error = TEXT("Filter cannot run in sample stage");
		return false;
	}

	bool EvaluateFilter(TConstArrayView<int16> Halo, int32 Start, FVoxelLandOperatorOutput& Output,
		TFunctionRef<bool()> IsCancelled, FString& Error) override
	{
		constexpr int32 HaloAxis = 37;
		constexpr int32 OutputAxis = 35;
		if (Halo.Num() != HaloAxis * HaloAxis * HaloAxis || Start < 0 || Output.Num() < 1 || Output.Num() > 256
			|| Start + Output.Num() > OutputAxis * OutputAxis * OutputAxis)
		{
			Error = TEXT("Invalid filter halo or output range");
			return false;
		}
		for (int32 I = 0; I < Output.Num(); ++I)
		{
			if (IsCancelled())
			{
				Error = TEXT("Cancelled");
				return false;
			}
			const int32 Index = Start + I;
			const int32 X = Index % OutputAxis, Y = (Index / OutputAxis) % OutputAxis,
						Z = Index / (OutputAxis * OutputAxis);
			const int32 Center = (X + 1) + HaloAxis * ((Y + 1) + HaloAxis * (Z + 1));
			const int32 Sum = int32(Halo[Center - 1]) + int32(Halo[Center]) + int32(Halo[Center + 1]);
			if (!Output.WriteDensity(I, int16(Sum < 0 ? -((-Sum + 1) / 3) : (Sum + 1) / 3)))
			{
				Error = TEXT("Invalid filter output");
				return false;
			}
		}
		return true;
	}
};

class FExampleFilter final : public IVoxelLandOperator
{
public:
	EVoxelLandOperatorStage Stage() const override { return EVoxelLandOperatorStage::OneCellFilter; }
	EVoxelLandWorldRequirement WorldRequirement() const override
	{
		return EVoxelLandWorldRequirement::BoundedNeighborhood;
	}
	FGuid Id() const override { return VoxelLandExampleFilterOperatorId(); }
	int32 Version() const override { return 1; }
	TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
	EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
	uint32 MaximumWorkerBytes() const override { return sizeof(FExampleFilterWorker); }

	bool ValidateConfiguration(TConstArrayView<uint8> Config, FString& Error) const override
	{
		if (!Config.IsEmpty())
		{
			Error = TEXT("Example filter takes no configuration");
			return false;
		}
		return true;
	}

	TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(
		TConstArrayView<uint8> Config, uint64, FString& Error) const override
	{
		if (!ValidateConfiguration(Config, Error))
		{
			return nullptr;
		}
		return MakeUnique<FExampleFilterWorker>();
	}
};
}

bool RegisterVoxelLandExampleFilter(FString& Error)
{
	return FVoxelLandOperatorRegistry::RegisterV3(TEXT("VoxelLandAlgo"),
		MakeShared<FExampleFilter, ESPMode::ThreadSafe>(), FVoxelLandOperatorRegistry::ProviderAbiVersion, Error);
}

bool UnregisterVoxelLandExampleFilter(FString& Error)
{
	return FVoxelLandOperatorRegistry::Unregister(VoxelLandExampleFilterOperatorId(), 1, Error);
}