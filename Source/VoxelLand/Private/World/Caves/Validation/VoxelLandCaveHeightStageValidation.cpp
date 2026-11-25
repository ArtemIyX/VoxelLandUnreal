#include "World/Caves/Validation/VoxelLandCaveHeightStageValidation.h"

#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

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
		if (!ValidateProjection(Snapshot))
		{
			return FailureCode == EVoxelLandM5ErrorCode::Cancelled
					 ? Failure()
					 : Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("M4 height projections do not match"));
		}
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Height projection cancelled"));
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

	bool InspectString(const FString& Value)
	{
		if (!Add(Value.Len(), MaxVariableUnits, VariableUnits))
		{
			return false;
		}
		if (!Budget->TryChargeWork(Value.Len()))
		{
			FailureCode = EVoxelLandM5ErrorCode::BudgetExceeded;
			FailureMessage = TEXT("Height projection string work exhausted");
			return false;
		}
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			if ((Index & 255) == 0 && !CompareStep())
			{
				return false;
			}
			if (Value[Index] == 0)
			{
				FailureCode = EVoxelLandM5ErrorCode::InvalidInput;
				FailureMessage = TEXT("Height projection string contains an embedded null");
				return false;
			}
		}
		return true;
	}

	bool InspectSubPath(const FSoftObjectPath& Path)
	{
		const int32 Length = Path.GetSubPathUtf8String().Len();
		if (!Add(Length, MaxVariableUnits, VariableUnits))
		{
			return false;
		}
		if (!Budget->TryChargeWork(Length))
		{
			FailureCode = EVoxelLandM5ErrorCode::BudgetExceeded;
			FailureMessage = TEXT("Height projection path work exhausted");
			return false;
		}
		for (int32 Index = 0; Index < Length; ++Index)
		{
			if ((Index & 255) == 0 && !CompareStep())
			{
				return false;
			}
			if (Path.GetSubPathUtf8String()[Index] == 0)
			{
				FailureCode = EVoxelLandM5ErrorCode::InvalidInput;
				FailureMessage = TEXT("Height projection path contains an embedded null");
				return false;
			}
		}
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
				|| !InspectSubPath(Node.FunctionPath))
			{
				return false;
			}
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (!Step() || !InspectString(Pin.Name))
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

	bool InspectParameters(const TArray<FVoxelLandGraphParameter>& Parameters)
	{
		if (!Add(Parameters.Num(), MaxRecords, Records))
		{
			return false;
		}
		for (const FVoxelLandGraphParameter& Parameter : Parameters)
		{
			if (!Step() || !InspectString(Parameter.Name))
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
		if (!InspectParameters(Source.Parameters) || !InspectLinks(Source.Links)
			|| !Add(Source.Functions.Num(), MaxRecords, Records) || !InspectNodes(Source.Nodes))
		{
			return false;
		}
		for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
		{
			if (!Step())
			{
				return false;
			}
			if (!InspectString(Function.AssetPath) || !InspectParameters(Function.Inputs)
				|| !InspectLinks(Function.Links) || !InspectNodes(Function.Nodes))
			{
				return false;
			}
		}
		return true;
	}

	bool CompareStep()
	{
		if (!IsCancelled())
		{
			return true;
		}
		FailureCode = EVoxelLandM5ErrorCode::Cancelled;
		FailureMessage = TEXT("Height projection cancelled");
		return false;
	}

	static bool SameFloat(float A, float B)
	{
		return FMath::IsFinite(A) && FMath::IsFinite(B) && FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
	}

	bool SameParameters(const TArray<FVoxelLandGraphParameter>& A, const TArray<FVoxelLandGraphParameter>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!CompareStep())
			{
				return false;
			}
			const FVoxelLandGraphParameter& Left = A[Index];
			const FVoxelLandGraphParameter& Right = B[Index];
			if (Left.Id != Right.Id || !Left.Name.Equals(Right.Name, ESearchCase::CaseSensitive)
				|| Left.Type != Right.Type || !SameFloat(Left.FloatDefault, Right.FloatDefault)
				|| Left.IntDefault != Right.IntDefault || Left.BoolDefault != Right.BoolDefault
				|| Left.bHasRange != Right.bHasRange || !SameFloat(Left.FloatMin, Right.FloatMin)
				|| !SameFloat(Left.FloatMax, Right.FloatMax) || Left.IntMin != Right.IntMin
				|| Left.IntMax != Right.IntMax)
			{
				return false;
			}
		}
		return true;
	}

	bool SameNode(const FVoxelLandGraphNodeRecord& A, const FVoxelLandGraphNodeRecord& B)
	{
		if (!CompareStep() || A.Id != B.Id || A.OperatorId != B.OperatorId || A.OperatorVersion != B.OperatorVersion
			|| A.SeedSalt != B.SeedSalt || A.Configuration != B.Configuration || A.FunctionPath != B.FunctionPath
			|| A.ParameterId != B.ParameterId || A.Pins.Num() != B.Pins.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Pins.Num(); ++Index)
		{
			if (!CompareStep())
			{
				return false;
			}
			const FVoxelLandGraphPinRecord& Left = A.Pins[Index];
			const FVoxelLandGraphPinRecord& Right = B.Pins[Index];
			if (Left.Id != Right.Id || Left.InterfaceId != Right.InterfaceId
				|| !Left.Name.Equals(Right.Name, ESearchCase::CaseSensitive) || Left.Type != Right.Type
				|| Left.bOutput != Right.bOutput || !SameFloat(Left.FloatDefault, Right.FloatDefault)
				|| Left.IntDefault != Right.IntDefault || Left.BoolDefault != Right.BoolDefault
				|| Left.bInvalidDefault != Right.bInvalidDefault)
			{
				return false;
			}
		}
		return true;
	}

	bool SameLink(const FVoxelLandGraphLinkRecord& A, const FVoxelLandGraphLinkRecord& B)
	{
		return CompareStep() && A.SourceNode == B.SourceNode && A.SourcePin == B.SourcePin
			&& A.TargetNode == B.TargetNode && A.TargetPin == B.TargetPin;
	}

	bool SameFunctions(
		const TArray<FVoxelLandGraphFunctionSnapshot>& A, const TArray<FVoxelLandGraphFunctionSnapshot>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!CompareStep())
			{
				return false;
			}
			const FVoxelLandGraphFunctionSnapshot& Left = A[Index];
			const FVoxelLandGraphFunctionSnapshot& Right = B[Index];
			if (!Left.AssetPath.Equals(Right.AssetPath, ESearchCase::CaseSensitive)
				|| Left.SourceVersion != Right.SourceVersion || Left.OutputType != Right.OutputType
				|| Left.OutputNode != Right.OutputNode || Left.OutputPin != Right.OutputPin
				|| !SameParameters(Left.Inputs, Right.Inputs) || Left.Nodes.Num() != Right.Nodes.Num()
				|| Left.Links.Num() != Right.Links.Num())
			{
				return false;
			}
			for (int32 Node = 0; Node < Left.Nodes.Num(); ++Node)
			{
				if (!SameNode(Left.Nodes[Node], Right.Nodes[Node]))
				{
					return false;
				}
			}
			for (int32 Link = 0; Link < Left.Links.Num(); ++Link)
			{
				if (!SameLink(Left.Links[Link], Right.Links[Link]))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool SameProjectedNodes(
		const TArray<FVoxelLandGraphNodeRecord>& A, const TArray<FVoxelLandGraphNodeRecord>& B, const FGuid& OutputId)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		int32 Next = 0;
		for (const FVoxelLandGraphNodeRecord& Node : A)
		{
			if (!CompareStep())
			{
				return false;
			}
			if (Node.Id == OutputId)
			{
				continue;
			}
			while (Next < B.Num() && B[Next].Id == OutputId)
			{
				if (!CompareStep())
				{
					return false;
				}
				++Next;
			}
			if (!B.IsValidIndex(Next) || !SameNode(Node, B[Next++]))
			{
				return false;
			}
		}
		while (Next < B.Num() && B[Next].Id == OutputId)
		{
			if (!CompareStep())
			{
				return false;
			}
			++Next;
		}
		return Next == B.Num();
	}

	bool SameProjectedLinks(
		const TArray<FVoxelLandGraphLinkRecord>& A, const TArray<FVoxelLandGraphLinkRecord>& B, const FGuid& OutputId)
	{
		int32 Next = 0;
		for (const FVoxelLandGraphLinkRecord& Link : A)
		{
			if (!CompareStep())
			{
				return false;
			}
			if (Link.TargetNode == OutputId)
			{
				continue;
			}
			while (Next < B.Num() && B[Next].TargetNode == OutputId)
			{
				if (!CompareStep())
				{
					return false;
				}
				++Next;
			}
			if (!B.IsValidIndex(Next) || !SameLink(Link, B[Next++]))
			{
				return false;
			}
		}
		while (Next < B.Num() && B[Next].TargetNode == OutputId)
		{
			if (!CompareStep())
			{
				return false;
			}
			++Next;
		}
		return Next == B.Num();
	}

	bool SameSource(const FVoxelLandGraphSnapshot& A, const FVoxelLandGraphSnapshot& B, const FGuid& OutputId)
	{
		return A.SourceVersion == B.SourceVersion && SameParameters(A.Parameters, B.Parameters)
			&& SameFunctions(A.Functions, B.Functions) && SameProjectedNodes(A.Nodes, B.Nodes, OutputId)
			&& SameProjectedLinks(A.Links, B.Links, OutputId);
	}

	bool ValidateProjection(const FVoxelLandM4WorldSnapshot& Snapshot)
	{
		const FVoxelLandM4HeightStageManifest& Stage = Snapshot.HeightStage;
		const FVoxelLandGraphSnapshot* Sources[] = {
			&Snapshot.Continental.Source, &Snapshot.Mountain.Source, &Snapshot.Peaks.Source};
		const FGuid Inputs[] = {Stage.ContinentalInputId, Stage.MountainInputId, Stage.PeaksInputId};
		if (!Inputs[0].IsValid() || !Inputs[1].IsValid() || !Inputs[2].IsValid() || Inputs[0] == Inputs[1]
			|| Inputs[0] == Inputs[2] || Inputs[1] == Inputs[2])
		{
			return false;
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sources); ++Index)
		{
			const FVoxelLandGraphNodeRecord* Output = nullptr;
			for (const FVoxelLandGraphNodeRecord& Node : Sources[Index]->Nodes)
			{
				if (!CompareStep())
				{
					return false;
				}
				if (Node.Id == Stage.OutputNodeId)
				{
					Output = &Node;
					break;
				}
			}
			if (!Output || Output->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput)
				|| Output->OperatorVersion != 1 || Output->Pins.Num() != 1 || Output->Pins[0].bOutput
				|| Output->Pins[0].Type != EVoxelLandGraphValueType::Float || Output->Pins[0].Id != Inputs[Index])
			{
				return false;
			}
			if (Index > 0 && !SameSource(*Sources[0], *Sources[Index], Stage.OutputNodeId))
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
