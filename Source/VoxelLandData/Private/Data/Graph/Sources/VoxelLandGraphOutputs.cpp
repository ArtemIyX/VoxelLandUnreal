#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"

#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"

class FVoxelLandOutputProjection final
{
public:
	explicit FVoxelLandOutputProjection(FVoxelLandGraphSnapshot& InGraph)
		: Graph(InGraph)
	{
		for (const FVoxelLandGraphNodeRecord& Node : Graph.Nodes)
		{
			Ids.Add(Node.Id);
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				Ids.Add(Pin.Id);
			}
		}
	}

	void AddWorldCoordinates()
	{
		const int32 Count = Graph.Nodes.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FVoxelLandGraphNodeRecord& Node = Graph.Nodes[Index];
			if (Node.OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::WorldNoise2D) || Node.Pins.Num() < 2)
			{
				continue;
			}
			Node.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::Value2);
			const FGuid Id = Node.Id;
			const FGuid Inputs[] = {Node.Pins[0].Id, Node.Pins[1].Id};
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				if (!Graph.Links.ContainsByPredicate([&Inputs, Axis](const FVoxelLandGraphLinkRecord& Link) {
						return Link.TargetPin == Inputs[Axis];
					}))
				{
					const FVoxelLandGraphNodeRecord& Position =
						AddNode(Axis == 0 ? EVoxelLandBuiltinNode::PositionX : EVoxelLandBuiltinNode::PositionY);
					Link(Position.Id, Position.Pins[0].Id, Id, Inputs[Axis]);
				}
			}
		}
	}

	void RemoveOutputNodes()
	{
		TSet<FGuid> Removed;
		Graph.Nodes.RemoveAll([&Removed](const FVoxelLandGraphNodeRecord& Node) {
			if (!FVoxelLandGraphOutputs::IsNamedOutput(Node.OperatorId)
				&& Node.OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
			{
				return false;
			}
			Removed.Add(Node.Id);
			return true;
		});
		Graph.Links.RemoveAll([&Removed](const FVoxelLandGraphLinkRecord& Link) {
			return Removed.Contains(Link.SourceNode) || Removed.Contains(Link.TargetNode);
		});
	}

	void AddResult(FGuid Source, FGuid Pin, EVoxelLandGraphValueType Type)
	{
		if (Type == EVoxelLandGraphValueType::Int32)
		{
			FVoxelLandGraphNodeRecord& Conversion = AddNode(EVoxelLandBuiltinNode::IntToFloat);
			FVoxelLandGraphPinRecord Input = MakePin(EVoxelLandGraphValueType::Int32, false);
			Conversion.Pins.Insert(Input, 0);
			Link(Source, Pin, Conversion.Id, Input.Id);
			Source = Conversion.Id;
			Pin = Conversion.Pins.Last().Id;
		}
		if (Type == EVoxelLandGraphValueType::Bool)
		{
			FVoxelLandGraphNodeRecord& Conversion = AddNode(EVoxelLandBuiltinNode::SelectFloat);
			const FVoxelLandGraphPinRecord Input = MakePin(EVoxelLandGraphValueType::Bool, false);
			FVoxelLandGraphPinRecord TrueValue = MakePin(EVoxelLandGraphValueType::Float, false);
			TrueValue.FloatDefault = 1.f;
			Conversion.Pins.Insert(Input, 0);
			Conversion.Pins.Insert(TrueValue, 1);
			Conversion.Pins.Insert(MakePin(EVoxelLandGraphValueType::Float, false), 2);
			Link(Source, Pin, Conversion.Id, Input.Id);
			Source = Conversion.Id;
			Pin = Conversion.Pins.Last().Id;
		}
		FVoxelLandGraphNodeRecord& Sink = AddNode(EVoxelLandBuiltinNode::DensityOutput);
		Sink.Pins[0].bOutput = false;
		Link(Source, Pin, Sink.Id, Sink.Pins[0].Id);
	}

private:
	FGuid MakeId()
	{
		FGuid Id;
		do
		{
			Id = FGuid(0x564c4f55, 0x54505554, 1, Next++);
		}
		while (Ids.Contains(Id));
		Ids.Add(Id);
		return Id;
	}

	FVoxelLandGraphPinRecord MakePin(EVoxelLandGraphValueType Type, bool bOutput)
	{
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = MakeId();
		Pin.Type = Type;
		Pin.bOutput = bOutput;
		return Pin;
	}

	FVoxelLandGraphNodeRecord& AddNode(EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.Id = MakeId();
		Node.OperatorId = VoxelLandBuiltinNodeId(Kind);
		Node.Pins.Add(MakePin(EVoxelLandGraphValueType::Float, true));
		return Node;
	}

	void Link(FGuid Source, FGuid SourcePin, FGuid Target, FGuid TargetPin)
	{
		FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
		Link.SourceNode = Source;
		Link.SourcePin = SourcePin;
		Link.TargetNode = Target;
		Link.TargetPin = TargetPin;
	}

	FVoxelLandGraphSnapshot& Graph;
	TSet<FGuid> Ids;
	uint32 Next = 1;
};

bool FVoxelLandGraphOutputs::IsNamedOutput(const FGuid& Operator)
{
	return Operator == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatOutput)
		|| Operator == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::IntOutput)
		|| Operator == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::BoolOutput);
}

FString FVoxelLandGraphOutputs::GetName(const FVoxelLandGraphNodeRecord& Node)
{
	if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
	{
		return TEXT("Density");
	}
	if (Node.Configuration.IsEmpty())
	{
		return TEXT("Output");
	}
	const FUTF8ToTCHAR Name(reinterpret_cast<const ANSICHAR*>(Node.Configuration.GetData()), Node.Configuration.Num());
	return FString(Name.Length(), Name.Get());
}

FString FVoxelLandGraphOutputs::GetName(const FVoxelLandGraphNodeRecord& Node, const FGuid& SelectedOutput)
{
	return Node.Id == SelectedOutput ? GetName(Node) : FString();
}

void FVoxelLandGraphOutputs::SetName(FVoxelLandGraphNodeRecord& Node, const FString& Name)
{
	const FTCHARToUTF8 UTF8(*Name);
	Node.Configuration.Reset(UTF8.Length());
	Node.Configuration.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
}

bool FVoxelLandGraphOutputs::HasNamedOutputs(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes)
{
	return Nodes.ContainsByPredicate(
		[](const FVoxelLandGraphNodeRecord& Node) { return IsNamedOutput(Node.OperatorId); });
}

bool FVoxelLandGraphOutputs::Project(
	FVoxelLandGraphSnapshot& Snapshot, const FGuid& Output, bool bForPreview, FString& Error)
{
	const FVoxelLandGraphNodeRecord* Node =
		Snapshot.Nodes.FindByPredicate([&Output](const FVoxelLandGraphNodeRecord& N) {
			return Output.IsValid() ? N.Id == Output
									: N.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		});
	if (!Node
		|| (!IsNamedOutput(Node->OperatorId)
			&& Node->OperatorId != VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput)))
	{
		Error = TEXT("The selected terrain output is missing.");
		return false;
	}
	if (Node->OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput)
		&& !HasNamedOutputs(Snapshot.Nodes))
	{
		FVoxelLandOutputProjection Projection(Snapshot);
		Projection.AddWorldCoordinates();
		return true;
	}
	if (Node->Pins.Num() != 1 || (!bForPreview && Node->Pins[0].Type != EVoxelLandGraphValueType::Float))
	{
		Error = FString::Printf(TEXT("Output '%s' must have a Float input for terrain generation."), *GetName(*Node));
		return false;
	}
	const FGuid Target = Node->Id, Input = Node->Pins[0].Id;
	const FVoxelLandGraphLinkRecord* Wire = Snapshot.Links.FindByPredicate(
		[&](const FVoxelLandGraphLinkRecord& L) { return L.TargetNode == Target && L.TargetPin == Input; });
	if (!Wire)
	{
		Error =
			FString::Printf(TEXT("Connect a value to output '%s'. For first terrain, try 2D Noise."), *GetName(*Node));
		return false;
	}
	const FGuid Source = Wire->SourceNode, Pin = Wire->SourcePin;
	return ProjectPin(Snapshot, Source, Pin, Error);
}

bool FVoxelLandGraphOutputs::ProjectPin(
	FVoxelLandGraphSnapshot& Snapshot, const FGuid& Node, const FGuid& Pin, FString& Error)
{
	const FVoxelLandGraphNodeRecord* Source =
		Snapshot.Nodes.FindByPredicate([&Node](const FVoxelLandGraphNodeRecord& N) { return N.Id == Node; });
	const FVoxelLandGraphPinRecord* Value =
		Source ? Source->Pins.FindByPredicate(
					 [&Pin](const FVoxelLandGraphPinRecord& P) { return P.Id == Pin && P.bOutput; })
			   : nullptr;
	if (!Value)
	{
		Error = TEXT("The selected output pin is missing.");
		return false;
	}
	const EVoxelLandGraphValueType Type = Value->Type;
	FVoxelLandOutputProjection Projection(Snapshot);
	Projection.RemoveOutputNodes();
	Projection.AddWorldCoordinates();
	Projection.AddResult(Node, Pin, Type);
	return true;
}
