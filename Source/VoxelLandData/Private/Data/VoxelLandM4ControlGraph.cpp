#include "Data/Assets/VoxelLandM4ControlGraph.h"

#include "Data/Enums/VoxelLandBuiltinNode.h"
#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

#if WITH_EDITOR
void UVoxelLandM4ControlGraph::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	const FObjectSavePackageSerializeContext* Context = Ar.GetSavePackageSerializeContext();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Ar.IsSaving() && Ar.IsCooking() && Context
		&& Context->GetPhase() == EObjectSaveContextPhase::Write && !bCookPreparationSucceeded)
	{
		Ar.SetError();
	}
}

void UVoxelLandM4ControlGraph::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);
	if (ObjectSaveContext.IsLastConcurrentSave())
	{
		bCookPreparationSucceeded = false;
	}
}
#endif

FGuid UVoxelLandM4ControlGraph::GetOutputId(EVoxelLandM4ControlChannel Channel) const
{
	if (HeightOutputId.IsValid()
		&& (Channel == EVoxelLandM4ControlChannel::Continental || Channel == EVoxelLandM4ControlChannel::Mountain
			|| Channel == EVoxelLandM4ControlChannel::Peaks))
	{
		return HeightOutputId;
	}
	switch (Channel)
	{
		case EVoxelLandM4ControlChannel::Continental:
			return ContinentalOutputId;
		case EVoxelLandM4ControlChannel::Mountain:
			return MountainOutputId;
		case EVoxelLandM4ControlChannel::Peaks:
			return PeaksOutputId;
		default:
			return {};
	}
}

const FVoxelLandSerializedProgram& UVoxelLandM4ControlGraph::GetProgram(EVoxelLandM4ControlChannel Channel) const
{
	switch (Channel)
	{
		case EVoxelLandM4ControlChannel::Continental:
			return ContinentalProgram;
		case EVoxelLandM4ControlChannel::Mountain:
			return MountainProgram;
		case EVoxelLandM4ControlChannel::Peaks:
			return PeaksProgram;
		default:
			return CompiledProgram; // Callers validate the channel before use.
	}
}

bool UVoxelLandM4ControlGraph::ProjectSnapshot(const FVoxelLandGraphSnapshot& Source,
	EVoxelLandM4ControlChannel Channel, FVoxelLandGraphSnapshot& OutProjection, FString& OutError) const
{
	OutError.Reset();
	if (HeightOutputId.IsValid())
	{
		const int32 PinIndex = Channel == EVoxelLandM4ControlChannel::Continental ? 0
							 : Channel == EVoxelLandM4ControlChannel::Mountain    ? 1
							 : Channel == EVoxelLandM4ControlChannel::Peaks       ? 2
																				  : INDEX_NONE;
		const FVoxelLandGraphNodeRecord* Height = nullptr;
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
			{
				OutError = TEXT("Remove legacy control outputs when using an M4 height output.");
				return false;
			}
			if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::M4HeightOutput))
			{
				if (Height || Node.Id != HeightOutputId)
				{
					OutError = TEXT("M4 graph requires exactly one selected height output.");
					return false;
				}
				Height = &Node;
			}
		}
		if (!Height || PinIndex == INDEX_NONE || Height->OperatorVersion != 1 || Height->Pins.Num() != 3
			|| !Height->Configuration.IsEmpty())
		{
			OutError = TEXT("Missing or unsupported M4 height output.");
			return false;
		}
		for (const FVoxelLandGraphPinRecord& Pin : Height->Pins)
		{
			if (Pin.bOutput || Pin.Type != EVoxelLandGraphValueType::Float)
			{
				OutError = TEXT("M4 height output requires three float inputs.");
				return false;
			}
		}
		const FGuid SelectedPin = Height->Pins[PinIndex].Id;
		FVoxelLandGraphSnapshot Projection = Source;
		for (FVoxelLandGraphNodeRecord& Node : Projection.Nodes)
		{
			if (Node.Id == HeightOutputId)
			{
				const FVoxelLandGraphPinRecord Selected = Node.Pins[PinIndex];
				Node.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
				Node.Pins = {Selected};
				break;
			}
		}
		Projection.Links.RemoveAll([this, SelectedPin](const FVoxelLandGraphLinkRecord& Link) {
			return Link.TargetNode == HeightOutputId && Link.TargetPin != SelectedPin;
		});
		OutProjection = MoveTemp(Projection);
		return true;
	}
	const FGuid Selected = GetOutputId(Channel);
	if (!Selected.IsValid() || !ContinentalOutputId.IsValid() || !MountainOutputId.IsValid() || !PeaksOutputId.IsValid()
		|| ContinentalOutputId == MountainOutputId || ContinentalOutputId == PeaksOutputId
		|| MountainOutputId == PeaksOutputId)
	{
		OutError = TEXT("M4 graph requires three distinct named output nodes.");
		return false;
	}
	TSet<FGuid> Terminals;
	for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
	{
		if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
		{
			if (Node.Id != ContinentalOutputId && Node.Id != MountainOutputId && Node.Id != PeaksOutputId)
			{
				OutError = TEXT("M4 graph has an unassigned density output.");
				return false;
			}
			Terminals.Add(Node.Id);
		}
	}
	if (Terminals.Num() != 3 || !Terminals.Contains(Selected))
	{
		OutError = TEXT("M4 named outputs must reference three existing density outputs.");
		return false;
	}
	FVoxelLandGraphSnapshot Projection = Source;
	Projection.Nodes.RemoveAll([&Terminals, Selected](const FVoxelLandGraphNodeRecord& Node) {
		return Terminals.Contains(Node.Id) && Node.Id != Selected;
	});
	Projection.Links.RemoveAll([&Terminals, Selected](const FVoxelLandGraphLinkRecord& Link) {
		return (Terminals.Contains(Link.SourceNode) && Link.SourceNode != Selected)
			|| (Terminals.Contains(Link.TargetNode) && Link.TargetNode != Selected);
	});
	OutProjection = MoveTemp(Projection);
	return true;
}
