#include "World/VoxelLandM5HeightStageValidation.h"

#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"

class FVoxelLandM5HeightStageValidationInternals final
{
public:
	static constexpr int64 MaxRecords = 65536;
	static constexpr int64 MaxVariableUnits = 8 * 1024 * 1024;
	static constexpr int64 ScratchAllowance = 4096;
	static constexpr int64 WorkPerRecord = 64;
	static constexpr int64 WorkPerVariableUnit = 16;

	FVoxelLandM5HeightStageValidationInternals(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget, TFunctionRef<bool()> InIsCancelled)
		: Budget(InBudget)
		, IsCancelled(InIsCancelled)
	{
	}

	FVoxelLandM5Diagnostic Run(const FVoxelLandM4WorldSnapshot& Snapshot)
	{
		if (!Step())
		{
			return Failure();
		}
		const FVoxelLandM4HeightStageManifest& Stage = Snapshot.HeightStage;
		if (Stage.Version == 0 && !Stage.OutputNodeId.IsValid() && !Stage.ContinentalInputId.IsValid()
			&& !Stage.MountainInputId.IsValid() && !Stage.PeaksInputId.IsValid())
		{
			return {};
		}
		if (Stage.Version != 1 || !Stage.OutputNodeId.IsValid())
		{
			return Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Unsupported M4 height stage manifest"));
		}
		const FVoxelLandGraphSnapshot* Sources[] = {
			&Snapshot.Continental.Source, &Snapshot.Mountain.Source, &Snapshot.Peaks.Source};
		for (const FVoxelLandGraphSnapshot* Source : Sources)
		{
			if (!InspectSource(*Source))
			{
				return Failure();
			}
		}
		int64 RecordWork = 0;
		int64 VariableWork = 0;
		int64 Work = 0;
		using namespace VoxelLand::M5::CheckedMath;
		if (!TryMultiplyNonNegative(Records, WorkPerRecord, RecordWork)
			|| !TryMultiplyNonNegative(VariableUnits, WorkPerVariableUnit, VariableWork)
			|| !TryAdd(RecordWork, VariableWork, Work) || !TryAdd(Work, ScratchAllowance, Work))
		{
			return Error(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Height projection work overflow"));
		}
		if (!Budget->TryChargeWork(Work))
		{
			return Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Height projection work budget exhausted"));
		}
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(ScratchAllowance, Lease))
		{
			return Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Height projection scratch budget exhausted"));
		}
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Height projection cancelled"));
		}
		FString Reason;
		const bool bValid = VoxelLandM4HeightStageValidation::Validate(Snapshot, Reason);
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Height projection cancelled"));
		}
		if (!bValid)
		{
			return Error(EVoxelLandM5ErrorCode::InvalidInput, *Reason);
		}
		return {};
	}

private:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.HeightStage");
		Result.Message = Message;
		return Result;
	}

	FVoxelLandM5Diagnostic Failure() const { return Error(FailureCode, FailureMessage); }

	bool Step()
	{
		if (IsCancelled())
		{
			FailureCode = EVoxelLandM5ErrorCode::Cancelled;
			FailureMessage = TEXT("Height projection cancelled");
			return false;
		}
		if (!Budget->TryChargeWork(1))
		{
			FailureCode = EVoxelLandM5ErrorCode::BudgetExceeded;
			FailureMessage = TEXT("Height projection inventory work exhausted");
			return false;
		}
		return true;
	}

	bool Add(int64 Amount, int64 Limit, int64& Total)
	{
		int64 Result = 0;
		if (Amount < 0 || !VoxelLand::M5::CheckedMath::TryAdd(Total, Amount, Result) || Result > Limit)
		{
			FailureCode = EVoxelLandM5ErrorCode::BudgetExceeded;
			FailureMessage = TEXT("Height projection input exceeds bounded comparison limits");
			return false;
		}
		Total = Result;
		return true;
	}

	bool InspectNodes(const TArray<FVoxelLandGraphNodeRecord>& Nodes)
	{
		if (!Add(Nodes.Num(), MaxRecords, Records))
		{
			return false;
		}
		for (const FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			if (!Step() || !Add(Node.Pins.Num(), MaxRecords, Records)
				|| !Add(Node.Configuration.Num(), MaxVariableUnits, VariableUnits))
			{
				return false;
			}
			const FTopLevelAssetPath Path = Node.FunctionPath.GetAssetPath();
			if (!Add(Path.GetPackageName().GetStringLength(), MaxVariableUnits, VariableUnits)
				|| !Add(Path.GetAssetName().GetStringLength(), MaxVariableUnits, VariableUnits)
				|| !Add(Node.FunctionPath.GetSubPathUtf8String().Len(), MaxVariableUnits, VariableUnits))
			{
				return false;
			}
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (!Step() || !Add(Pin.Name.Len(), MaxVariableUnits, VariableUnits))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool InspectLinks(const TArray<FVoxelLandGraphLinkRecord>& Links)
	{
		if (!Add(Links.Num(), MaxRecords, Records))
		{
			return false;
		}
		for (int32 Index = 0; Index < Links.Num(); ++Index)
		{
			if (!Step())
			{
				return false;
			}
		}
		return true;
	}

	bool InspectSource(const FVoxelLandGraphSnapshot& Source)
	{
		if (!Step())
		{
			return false;
		}
		if (!Source.Parameters.IsEmpty())
		{
			FailureCode = EVoxelLandM5ErrorCode::Unproven;
			FailureMessage = TEXT("Height projection parameter FText histories are not metered");
			return false;
		}
		if (!InspectLinks(Source.Links) || !Add(Source.Functions.Num(), MaxRecords, Records)
			|| !InspectNodes(Source.Nodes))
		{
			return false;
		}
		for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
		{
			if (!Step())
			{
				return false;
			}
			if (!Function.Inputs.IsEmpty())
			{
				FailureCode = EVoxelLandM5ErrorCode::Unproven;
				FailureMessage = TEXT("Height projection function input FText histories are not metered");
				return false;
			}
			if (!Add(Function.AssetPath.Len(), MaxVariableUnits, VariableUnits) || !InspectLinks(Function.Links)
				|| !InspectNodes(Function.Nodes))
			{
				return false;
			}
		}
		return true;
	}

	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TFunctionRef<bool()> IsCancelled;
	int64 Records = 0;
	int64 VariableUnits = 0;
	EVoxelLandM5ErrorCode FailureCode = EVoxelLandM5ErrorCode::Unproven;
	const TCHAR* FailureMessage = TEXT("Height projection is unproven");
};

FVoxelLandM5Diagnostic FVoxelLandM5HeightStageValidation::Validate(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	FVoxelLandM5HeightStageValidationInternals Validation(Budget, IsCancelled);
	return Validation.Run(Snapshot);
}
