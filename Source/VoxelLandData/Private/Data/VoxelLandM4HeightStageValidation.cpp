#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"

class FVoxelLandM4HeightStageValidationInternals final
{
public:
	template <typename T> static bool M4StageArrayEqual(const TArray<T>& A, const TArray<T>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!T::StaticStruct()->CompareScriptStruct(&A[Index], &B[Index], 0))
			{
				return false;
			}
		}
		return true;
	}

	static bool M4StageSameSource(
		const FVoxelLandGraphSnapshot& A, const FVoxelLandGraphSnapshot& B, const FGuid& OutputId)
	{
		if (A.SourceVersion != B.SourceVersion || !M4StageArrayEqual(A.Parameters, B.Parameters)
			|| !M4StageArrayEqual(A.Functions, B.Functions) || A.Nodes.Num() != B.Nodes.Num())
		{
			return false;
		}
		int32 NextNode = 0;
		for (const FVoxelLandGraphNodeRecord& Node : A.Nodes)
		{
			if (Node.Id == OutputId)
			{
				continue;
			}
			while (NextNode < B.Nodes.Num() && B.Nodes[NextNode].Id == OutputId)
			{
				++NextNode;
			}
			if (!B.Nodes.IsValidIndex(NextNode)
				|| !FVoxelLandGraphNodeRecord::StaticStruct()->CompareScriptStruct(&Node, &B.Nodes[NextNode++], 0))
			{
				return false;
			}
		}
		while (NextNode < B.Nodes.Num() && B.Nodes[NextNode].Id == OutputId)
		{
			++NextNode;
		}
		if (NextNode != B.Nodes.Num())
		{
			return false;
		}
		int32 NextLink = 0;
		for (const FVoxelLandGraphLinkRecord& Link : A.Links)
		{
			if (Link.TargetNode == OutputId)
			{
				continue;
			}
			while (NextLink < B.Links.Num() && B.Links[NextLink].TargetNode == OutputId)
			{
				++NextLink;
			}
			if (!B.Links.IsValidIndex(NextLink)
				|| !FVoxelLandGraphLinkRecord::StaticStruct()->CompareScriptStruct(&Link, &B.Links[NextLink++], 0))
			{
				return false;
			}
		}
		while (NextLink < B.Links.Num() && B.Links[NextLink].TargetNode == OutputId)
		{
			++NextLink;
		}
		return NextLink == B.Links.Num();
	}
};

bool VoxelLandM4HeightStageValidation::Validate(const FVoxelLandM4WorldSnapshot& Snapshot, FString& OutError)
{
	OutError.Reset();
	const auto& Stage = Snapshot.HeightStage;
	if (Stage.Version == 0 && !Stage.OutputNodeId.IsValid() && !Stage.ContinentalInputId.IsValid()
	    && !Stage.MountainInputId.IsValid() && !Stage.PeaksInputId.IsValid())
	{
		return true;
	}
	if (Stage.Version != 1 || !Stage.OutputNodeId.IsValid())
	{
		OutError = TEXT("Unsupported M4 height stage manifest");
		return false;
	}
	const FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	const FGuid Inputs[] = {Stage.ContinentalInputId, Stage.MountainInputId, Stage.PeaksInputId};
	TSet<FGuid> InputPins;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVoxelLandM4ControlSnapshot* Control = Controls[Index];
		const FVoxelLandGraphNodeRecord* Node = Control->Source.Nodes.FindByPredicate(
			[&Stage](const FVoxelLandGraphNodeRecord& Item) { return Item.Id == Stage.OutputNodeId; });
		if (!Node || Node->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput)
		    || Node->OperatorVersion != 1 || Node->Pins.Num() != 1 || Node->Pins[0].bOutput
		    || Node->Pins[0].Type != EVoxelLandGraphValueType::Float || !Node->Pins[0].Id.IsValid()
		    || Node->Pins[0].Id != Inputs[Index] || InputPins.Contains(Node->Pins[0].Id))
		{
			OutError = TEXT("M4 height stage does not match its projected control inputs");
			return false;
		}
		InputPins.Add(Node->Pins[0].Id);
		if (Index > 0
			&& !FVoxelLandM4HeightStageValidationInternals::M4StageSameSource(
				Snapshot.Continental.Source, Control->Source, Stage.OutputNodeId))
		{
			OutError = TEXT("M4 height stage projections do not come from the same source graph");
			return false;
		}
	}
	return true;
}