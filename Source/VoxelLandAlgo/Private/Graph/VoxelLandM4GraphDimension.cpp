#include "Graph/VoxelLandM4GraphDimension.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandBuiltinCatalog.h"

namespace VoxelLandM4GraphDimension
{
namespace
{
FVoxelLandError GraphDimensionFailure(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidProgram;
	Error.Message = Message;
	return Error;
}

// Explicit allowlist: new builtin IDs do not inherit a 2D proof automatically.
bool GraphDimensionAllowed(EVoxelLandBuiltinNode Kind)
{
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::PositionX:
		case EVoxelLandBuiltinNode::PositionY:
		case EVoxelLandBuiltinNode::FloatConstant:
		case EVoxelLandBuiltinNode::FloatParameter:
		case EVoxelLandBuiltinNode::IntConstant:
		case EVoxelLandBuiltinNode::BoolConstant:
		case EVoxelLandBuiltinNode::IntParameter:
		case EVoxelLandBuiltinNode::BoolParameter:
		case EVoxelLandBuiltinNode::Add:
		case EVoxelLandBuiltinNode::Subtract:
		case EVoxelLandBuiltinNode::Multiply:
		case EVoxelLandBuiltinNode::Divide:
		case EVoxelLandBuiltinNode::Min:
		case EVoxelLandBuiltinNode::Max:
		case EVoxelLandBuiltinNode::Abs:
		case EVoxelLandBuiltinNode::Negate:
		case EVoxelLandBuiltinNode::Union:
		case EVoxelLandBuiltinNode::SubtractShape:
		case EVoxelLandBuiltinNode::Intersect:
		case EVoxelLandBuiltinNode::Clamp:
		case EVoxelLandBuiltinNode::Saturate:
		case EVoxelLandBuiltinNode::Lerp:
		case EVoxelLandBuiltinNode::IntToFloat:
		case EVoxelLandBuiltinNode::Less:
		case EVoxelLandBuiltinNode::LessEqual:
		case EVoxelLandBuiltinNode::Equal:
		case EVoxelLandBuiltinNode::BoolAnd:
		case EVoxelLandBuiltinNode::BoolOr:
		case EVoxelLandBuiltinNode::BoolNot:
		case EVoxelLandBuiltinNode::SelectFloat:
		case EVoxelLandBuiltinNode::SelectInt:
		case EVoxelLandBuiltinNode::SelectBool:
		case EVoxelLandBuiltinNode::Power:
		case EVoxelLandBuiltinNode::Perlin2:
		case EVoxelLandBuiltinNode::Fbm2:
		case EVoxelLandBuiltinNode::Value2:
		case EVoxelLandBuiltinNode::Simplex2:
		case EVoxelLandBuiltinNode::RerouteFloat:
		case EVoxelLandBuiltinNode::RerouteInt:
		case EVoxelLandBuiltinNode::RerouteBool:
			return true;
		default:
			return false;
	}
}

bool GraphDimensionGuidLess(const FGuid& A, const FGuid& B)
{
	if (A.A != B.A)
	{
		return A.A < B.A;
	}
	if (A.B != B.B)
	{
		return A.B < B.B;
	}
	if (A.C != B.C)
	{
		return A.C < B.C;
	}
	return A.D < B.D;
}
}

FVoxelLandError CollectProven2DBranch(
	const FVoxelLandGraphSnapshot& Snapshot, const FGuid& RootNode, TArray<FVoxelLandM4StageNode>& OutNodes)
{
	if (Snapshot.SourceVersion != 1 || Snapshot.Nodes.IsEmpty() || Snapshot.Nodes.Num() > 4096
		|| Snapshot.Links.Num() > 16384)
	{
		return GraphDimensionFailure(TEXT("Unsupported graph version or node/link count"));
	}
	TMap<FGuid, int32> NodeById;
	TMap<FGuid, const FVoxelLandGraphPinRecord*> PinById;
	TMap<FGuid, int32> PinOwner;
	for (int32 Index = 0; Index < Snapshot.Nodes.Num(); ++Index)
	{
		const FVoxelLandGraphNodeRecord& Node = Snapshot.Nodes[Index];
		if (!Node.Id.IsValid() || NodeById.Contains(Node.Id) || Node.Pins.Num() > 64)
		{
			return GraphDimensionFailure(TEXT("Invalid or duplicate graph node ID or pin count"));
		}
		NodeById.Add(Node.Id, Index);
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (!Pin.Id.IsValid() || PinById.Contains(Pin.Id) || Pin.bInvalidDefault
				|| !FMath::IsFinite(Pin.FloatDefault)
				|| (Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32
					&& Pin.Type != EVoxelLandGraphValueType::Bool))
			{
				return GraphDimensionFailure(TEXT("Invalid or duplicate graph pin ID, type or default"));
			}
			PinById.Add(Pin.Id, &Pin);
			PinOwner.Add(Pin.Id, Index);
		}
	}
	const int32* RootIndex = NodeById.Find(RootNode);
	if (!RootIndex)
	{
		return GraphDimensionFailure(TEXT("Requested M4 graph root is missing"));
	}
	TArray<TArray<int32>> Producers, Consumers;
	Producers.SetNum(Snapshot.Nodes.Num());
	Consumers.SetNum(Snapshot.Nodes.Num());
	TArray<int32> InDegree;
	InDegree.Init(0, Snapshot.Nodes.Num());
	TSet<FGuid> LinkedInputs;
	for (const FVoxelLandGraphLinkRecord& Link : Snapshot.Links)
	{
		const FVoxelLandGraphPinRecord* const* Source = PinById.Find(Link.SourcePin);
		const FVoxelLandGraphPinRecord* const* Target = PinById.Find(Link.TargetPin);
		const int32* SourceOwner = PinOwner.Find(Link.SourcePin);
		const int32* TargetOwner = PinOwner.Find(Link.TargetPin);
		if (!Source || !Target || !SourceOwner || !TargetOwner || Snapshot.Nodes[*SourceOwner].Id != Link.SourceNode
			|| Snapshot.Nodes[*TargetOwner].Id != Link.TargetNode || !(*Source)->bOutput || (*Target)->bOutput
			|| (*Source)->Type != (*Target)->Type || LinkedInputs.Contains(Link.TargetPin))
		{
			return GraphDimensionFailure(TEXT("Invalid pin ownership, direction, type or multiple input sources"));
		}
		LinkedInputs.Add(Link.TargetPin);
		Producers[*TargetOwner].Add(*SourceOwner);
		Consumers[*SourceOwner].Add(*TargetOwner);
		++InDegree[*TargetOwner];
	}
	TArray<int32> Ready;
	for (int32 Index = 0; Index < InDegree.Num(); ++Index)
	{
		if (InDegree[Index] == 0)
		{
			Ready.Add(Index);
		}
	}
	for (int32 Cursor = 0; Cursor < Ready.Num(); ++Cursor)
	{
		for (int32 Consumer : Consumers[Ready[Cursor]])
		{
			if (--InDegree[Consumer] == 0)
			{
				Ready.Add(Consumer);
			}
		}
	}
	if (Ready.Num() != Snapshot.Nodes.Num())
	{
		return GraphDimensionFailure(TEXT("Graph contains a cycle"));
	}

	TArray<bool> Seen;
	Seen.Init(false, Snapshot.Nodes.Num());
	TArray<int32> Pending = {*RootIndex};
	Seen[*RootIndex] = true;
	TArray<FVoxelLandM4StageNode> Result;
	while (!Pending.IsEmpty())
	{
		const int32 Index = Pending.Pop(EAllowShrinking::No);
		const FVoxelLandGraphNodeRecord& Node = Snapshot.Nodes[Index];
		if (Node.OperatorId.A != 0x564c4d33 || Node.OperatorId.B != 1 || Node.OperatorId.C != 0
			|| !Node.FunctionPath.IsNull())
		{
			return GraphDimensionFailure(TEXT("2D proof does not support providers or graph functions"));
		}
		const EVoxelLandBuiltinNode Kind = static_cast<EVoxelLandBuiltinNode>(Node.OperatorId.D);
		if (!GraphDimensionAllowed(Kind))
		{
			return GraphDimensionFailure(TEXT("Node is not a proven 2D builtin"));
		}
		const bool bVariadic = Kind == EVoxelLandBuiltinNode::Add || Kind == EVoxelLandBuiltinNode::Multiply;
		FVoxelLandBuiltinNodeSpec Spec;
		if (!FVoxelLandBuiltinCatalog::Find(Kind, Spec, bVariadic ? Node.Pins.Num() - 1 : 2)
			|| Node.OperatorVersion != Spec.OperatorVersion || !Node.Configuration.IsEmpty()
			|| Node.Pins.Num() != Spec.Pins.Num())
		{
			return GraphDimensionFailure(TEXT("Invalid builtin version, configuration or pin count"));
		}
		for (int32 PinIndex = 0; PinIndex < Node.Pins.Num(); ++PinIndex)
		{
			if (Node.Pins[PinIndex].Type != Spec.Pins[PinIndex].Type
				|| Node.Pins[PinIndex].bOutput != Spec.Pins[PinIndex].bOutput)
			{
				return GraphDimensionFailure(TEXT("Invalid builtin pin direction or type"));
			}
		}
		if (Node.Pins.IsEmpty() || !Node.Pins.Last().bOutput)
		{
			return GraphDimensionFailure(TEXT("Selected builtin must have a value output"));
		}
		FVoxelLandM4StageNode StageNode;
		StageNode.Id = Node.Id;
		StageNode.Stage = EVoxelLandM4StageKind::Global;
		StageNode.bUsesZ = false;
		TSet<FGuid> UniqueProducers;
		for (int32 Producer : Producers[Index])
		{
			const FGuid& ProducerId = Snapshot.Nodes[Producer].Id;
			if (!UniqueProducers.Contains(ProducerId))
			{
				UniqueProducers.Add(ProducerId);
				StageNode.Dependencies.Add(ProducerId);
			}
			if (!Seen[Producer])
			{
				Seen[Producer] = true;
				Pending.Add(Producer);
			}
		}
		StageNode.Dependencies.Sort(GraphDimensionGuidLess);
		Result.Add(MoveTemp(StageNode));
	}
	Result.Sort([](const FVoxelLandM4StageNode& A, const FVoxelLandM4StageNode& B) {
		return GraphDimensionGuidLess(A.Id, B.Id);
	});
	OutNodes = MoveTemp(Result);
	return {};
}
}
