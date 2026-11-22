#include "Data/Assets/VoxelLandM4ControlGraph.h"

#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandGraphCycle.h"
#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

namespace
{
bool M4ProjectionValidateSource(const FVoxelLandGraphSnapshot& Source, FString& Error)
{
	if (Source.SourceVersion != 1 || Source.Nodes.Num() > 128 || Source.Links.Num() > 2048
	    || Source.Parameters.Num() > 128 || Source.Functions.Num() > 128)
	{
		Error = TEXT("M4 control source exceeds projection limits or has an unsupported version.");
		return false;
	}
	int64 TotalNodes = Source.Nodes.Num();
	int64 TotalLinks = Source.Links.Num();
	int64 TotalPins = 0;
	for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
	{
		TotalPins += Node.Pins.Num();
	}
	for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
	{
		TotalNodes += Function.Nodes.Num();
		TotalLinks += Function.Links.Num();
		if (Function.Inputs.Num() > 128 || Function.AssetPath.Len() > 2048)
		{
			Error = TEXT("M4 function dependency exceeds projection limits.");
			return false;
		}
		for (const FVoxelLandGraphNodeRecord& Node : Function.Nodes)
		{
			TotalPins += Node.Pins.Num();
		}
	}
	if (TotalNodes > 128 || TotalLinks > 2048 || TotalPins > 1024)
	{
		Error = TEXT("M4 graph and function dependencies exceed projection limits.");
		return false;
	}
	TMap<FGuid, const FVoxelLandGraphNodeRecord*> Nodes;
	TMap<FGuid, const FVoxelLandGraphPinRecord*> Pins;
	TMap<FGuid, FGuid> Owners;
	int64 ConfigBytes = 0;
	for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
	{
		ConfigBytes += Node.Configuration.Num();
		if (!Node.Id.IsValid() || !Node.OperatorId.IsValid() || Node.OperatorVersion < 1 || Nodes.Contains(Node.Id)
		    || Node.Pins.Num() > 64 || Node.Configuration.Num() > 65536 || ConfigBytes > 1048576)
		{
			Error = TEXT("Invalid or duplicate M4 graph node or configuration.");
			return false;
		}
		Nodes.Add(Node.Id, &Node);
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id) || Pin.bInvalidDefault || !FMath::IsFinite(Pin.FloatDefault)
			    || (Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32
			        && Pin.Type != EVoxelLandGraphValueType::Bool))
			{
				Error = TEXT("Invalid or duplicate M4 graph pin.");
				return false;
			}
			Pins.Add(Pin.Id, &Pin);
			Owners.Add(Pin.Id, Node.Id);
		}
	}
	TSet<FGuid> ConnectedInputs;
	for (const FVoxelLandGraphLinkRecord& Link : Source.Links)
	{
		const auto SourcePin = Pins.Find(Link.SourcePin);
		const auto TargetPin = Pins.Find(Link.TargetPin);
		const auto SourceOwner = Owners.Find(Link.SourcePin);
		const auto TargetOwner = Owners.Find(Link.TargetPin);
		if (!Nodes.Contains(Link.SourceNode) || !Nodes.Contains(Link.TargetNode) || !SourcePin || !TargetPin
		    || !SourceOwner || !TargetOwner || *SourceOwner != Link.SourceNode || *TargetOwner != Link.TargetNode
		    || !(*SourcePin)->bOutput || (*TargetPin)->bOutput || (*SourcePin)->Type != (*TargetPin)->Type
		    || ConnectedInputs.Contains(Link.TargetPin))
		{
			Error = TEXT("Invalid M4 graph link or duplicate input connection.");
			return false;
		}
		ConnectedInputs.Add(Link.TargetPin);
	}
	FGuid Node, Pin;
	if (FVoxelLandGraphCycle::Find(Source.Nodes, Source.Links, Node, Pin))
	{
		Error = TEXT("M4 control graph contains a cycle.");
		return false;
	}
	return true;
}

bool M4ProjectionTerminalValid(const FVoxelLandGraphNodeRecord& Node, int32 Inputs)
{
	if (Node.OperatorVersion != 1 || !Node.Configuration.IsEmpty() || !Node.FunctionPath.IsNull()
	    || Node.Pins.Num() != Inputs)
	{
		return false;
	}
	for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
	{
		if (Pin.bOutput || Pin.Type != EVoxelLandGraphValueType::Float)
		{
			return false;
		}
	}
	return true;
}
}

void UVoxelLandM4ControlGraph::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_EDITOR
	const FObjectSavePackageSerializeContext* Context = Ar.GetSavePackageSerializeContext();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Ar.IsSaving() && Ar.IsCooking() && Context
	    && Context->GetPhase() == EObjectSaveContextPhase::Write && !bCookPreparationSucceeded)
	{
		Ar.SetError();
	}
#endif
}

#if WITH_EDITOR
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
	if (!M4ProjectionValidateSource(Source, OutError))
	{
		return false;
	}
	if (HeightOutputId.IsValid())
	{
		const int32 PinIndex = Channel == EVoxelLandM4ControlChannel::Continental
			                       ? 0
			                       : Channel == EVoxelLandM4ControlChannel::Mountain
			                       ? 1
			                       : Channel == EVoxelLandM4ControlChannel::Peaks
			                       ? 2
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
		if (!Height || PinIndex == INDEX_NONE || !M4ProjectionTerminalValid(*Height, 3))
		{
			OutError = TEXT("Missing or unsupported M4 height output.");
			return false;
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
		if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::M4HeightOutput))
		{
			OutError = TEXT("M4 height output cannot be mixed with legacy named outputs.");
			return false;
		}
		if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
		{
			if (!M4ProjectionTerminalValid(Node, 1))
			{
				OutError = TEXT("Invalid M4 named output terminal.");
				return false;
			}
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
