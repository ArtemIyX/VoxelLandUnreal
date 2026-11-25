#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflight.h"

#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflightLimits.h"
#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflightStats.h"

class FVoxelLandM5IdentityPreflightInternals final
{
public:
	static FVoxelLandM5Diagnostic M5IdentityPreflightFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.IdentityPreflight");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::IdentityPreflight::Inspect(const FVoxelLandM5WorldSnapshot& Snapshot,
	const FVoxelLandM5IdentityPreflightLimits& Limits, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5IdentityPreflightStats& OutStats)
{
	if (IsCancelled())
	{
		return FVoxelLandM5IdentityPreflightInternals::M5IdentityPreflightFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Identity preflight cancelled"));
	}
	if (Limits.MaxElements < 0 || Limits.MaxStringCodeUnits < 0 || Limits.MaxConfigurationBytes < 0)
	{
		return FVoxelLandM5IdentityPreflightInternals::M5IdentityPreflightFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Negative identity preflight limit"));
	}
	FVoxelLandM5IdentityPreflightStats Scratch;
	auto Add = [](int64 Value, int64 Limit, int64& Total) {
		int64 Next = 0;
		if (Value < 0 || !VoxelLand::M5::CheckedMath::TryAdd(Total, Value, Next) || Next > Limit)
		{
			return false;
		}
		Total = Next;
		return true;
	};
	EVoxelLandM5ErrorCode FailureCode = EVoxelLandM5ErrorCode::None;
	auto Step = [&]() {
		if (IsCancelled())
		{
			FailureCode = EVoxelLandM5ErrorCode::Cancelled;
			return false;
		}
		if (!Budget->TryChargeWork(1))
		{
			FailureCode = EVoxelLandM5ErrorCode::BudgetExceeded;
			return false;
		}
		return true;
	};
	auto Elements = [&](int32 Count) {
		return Add(Count, Limits.MaxElements, Scratch.Elements);
	};
	auto String = [&](const FString& Value) {
		return Add(Value.Len(), Limits.MaxStringCodeUnits, Scratch.StringCodeUnits);
	};
	auto Configuration = [&](int32 Count) {
		return Add(Count, Limits.MaxConfigurationBytes, Scratch.ConfigurationBytes);
	};
	auto Parameters = [&](const TArray<FVoxelLandGraphParameter>& Values) {
		if (!Elements(Values.Num()))
		{
			return false;
		}
		for (const auto& Value : Values)
		{
			if (!Step() || !String(Value.Name))
			{
				return false;
			}
		}
		return true;
	};
	auto Nodes = [&](const TArray<FVoxelLandGraphNodeRecord>& Values) {
		if (!Elements(Values.Num()))
		{
			return false;
		}
		for (const auto& Node : Values)
		{
			if (!Step() || !Configuration(Node.Configuration.Num()) || !Elements(Node.Pins.Num()))
			{
				return false;
			}
			Scratch.bHasOpaqueSoftPaths |= !Node.FunctionPath.IsNull();
			for (const auto& Pin : Node.Pins)
			{
				if (!Step() || !String(Pin.Name))
				{
					return false;
				}
			}
		}
		return true;
	};
	const FVoxelLandM4ControlSnapshot* Channels[] = {
		&Snapshot.Base.Continental, &Snapshot.Base.Mountain, &Snapshot.Base.Peaks};
	int32 Completed = 0;
	for (const auto* Channel : Channels)
	{
		if (!Step())
		{
			break;
		}
		const auto& Source = Channel->Source;
		const auto& Payload = Channel->Payload;
		if (!Parameters(Source.Parameters) || !Nodes(Source.Nodes) || !Elements(Source.Links.Num())
			|| !Elements(Source.Functions.Num()) || !Elements(Channel->Overrides.Num())
			|| !Elements(Channel->CurveKeys.Num()) || !Elements(Channel->CookedCurve.SourceKeys.Num())
			|| !Elements(Channel->CookedCurve.SamplesQ8.Num()) || !Elements(Payload.Instructions.Num())
			|| !Elements(Payload.Constants.Num()) || !Elements(Payload.Parameters.Num())
			|| !Elements(Payload.Seeds.Num()) || !Elements(Payload.Noise.Num())
			|| !Elements(Payload.ExternalOperators.Num()))
		{
			break;
		}
		bool bOk = true;
		for (const auto& Function : Source.Functions)
		{
			if (!Step() || !String(Function.AssetPath) || !Parameters(Function.Inputs) || !Nodes(Function.Nodes)
				|| !Elements(Function.Links.Num()))
			{
				bOk = false;
				break;
			}
		}
		if (!bOk)
		{
			break;
		}
		for (const auto& Parameter : Payload.Parameters)
		{
			if (!Step() || !String(Parameter.Name))
			{
				bOk = false;
				break;
			}
		}
		if (!bOk)
		{
			break;
		}
		for (const auto& Operator : Payload.ExternalOperators)
		{
			if (!Step() || !Elements(Operator.Inputs.Num()) || !Configuration(Operator.Configuration.Num()))
			{
				bOk = false;
				break;
			}
		}
		if (!bOk)
		{
			break;
		}
		++Completed;
	}
	if (Completed != UE_ARRAY_COUNT(Channels))
	{
		return FVoxelLandM5IdentityPreflightInternals::M5IdentityPreflightFailure(
			FailureCode == EVoxelLandM5ErrorCode::None ? EVoxelLandM5ErrorCode::BudgetExceeded : FailureCode,
			TEXT("Identity input inventory exceeds a limit, work budget, or was cancelled"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5IdentityPreflightInternals::M5IdentityPreflightFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Identity preflight cancelled"));
	}
	OutStats = Scratch;
	return {};
}
