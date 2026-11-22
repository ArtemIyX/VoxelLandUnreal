#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"

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
	}
	return true;
}
