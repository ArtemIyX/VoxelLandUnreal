#include "Data/Assets/VoxelLandTerrainGraph.h"

namespace
{
constexpr int32 MaxNodes = 4096;
constexpr int32 MaxPinsPerNode = 64;
constexpr int32 MaxLinks = 16384;
constexpr int32 MaxNodeConfigBytes = 65536;
constexpr int64 MaxGraphConfigBytes = 1048576;
}

namespace
{
class FGraphStructureValidator
{
public:
	FGraphStructureValidator(const UVoxelLandTerrainGraph& InGraph, FString& InError)
		: Graph(InGraph), OutError(InError) {}

	bool Validate()
	{
		if (Graph.SourceVersion != 1 || Graph.Nodes.Num() > MaxNodes || Graph.Links.Num() > MaxLinks)
		{
			OutError = TEXT("Unsupported graph version or graph exceeds record limits.");
			return false;
		}
		return BuildIndex() && ValidateLinks() && ValidateAcyclic();
	}

private:
	bool BuildIndex()
	{
		int64 TotalConfigBytes = 0;
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			const FVoxelLandGraphNodeRecord& Node = Graph.Nodes[Index];
			if (!Node.Id.IsValid() || !Node.OperatorId.IsValid() || Node.OperatorVersion < 1 ||
				NodeIndices.Contains(Node.Id) || Node.Pins.Num() > MaxPinsPerNode)
			{
				OutError = TEXT("Invalid or duplicate node ID, operator, version or pin count.");
				return false;
			}
			TotalConfigBytes += Node.Configuration.Num();
			if (Node.Configuration.Num() > MaxNodeConfigBytes || TotalConfigBytes > MaxGraphConfigBytes)
			{
				OutError = TEXT("Graph operator configuration exceeds its size limit.");
				return false;
			}
			NodeIndices.Add(Node.Id, Index);
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (!Pin.Id.IsValid() || Pins.Contains(Pin.Id) ||
					(Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32 &&
					 Pin.Type != EVoxelLandGraphValueType::Bool) || !FMath::IsFinite(Pin.FloatDefault))
				{
					OutError = TEXT("Invalid or duplicate pin ID, type or default.");
					return false;
				}
				Pins.Add(Pin.Id, &Pin);
				PinOwners.Add(Pin.Id, Node.Id);
			}
		}
		return true;
	}

	bool ValidateLinks()
	{
		TSet<FGuid> ConnectedInputs;
		Edges.SetNum(Graph.Nodes.Num());
		Incoming.Init(0, Graph.Nodes.Num());
		for (const FVoxelLandGraphLinkRecord& Link : Graph.Links)
		{
			const FVoxelLandGraphPinRecord* const* Source = Pins.Find(Link.SourcePin);
			const FVoxelLandGraphPinRecord* const* Target = Pins.Find(Link.TargetPin);
			const FGuid* SourceOwner = PinOwners.Find(Link.SourcePin);
			const FGuid* TargetOwner = PinOwners.Find(Link.TargetPin);
			const int32* SourceIndex = NodeIndices.Find(Link.SourceNode);
			const int32* TargetIndex = NodeIndices.Find(Link.TargetNode);
			if (!Source || !Target || !SourceOwner || !TargetOwner || !SourceIndex || !TargetIndex ||
				*SourceOwner != Link.SourceNode || *TargetOwner != Link.TargetNode || !(*Source)->bOutput ||
				(*Target)->bOutput || (*Source)->Type != (*Target)->Type || ConnectedInputs.Contains(Link.TargetPin))
			{
				OutError = TEXT("Invalid link: check pin ownership, direction, type and single input source.");
				return false;
			}
			ConnectedInputs.Add(Link.TargetPin);
			Edges[*SourceIndex].Add(*TargetIndex);
			++Incoming[*TargetIndex];
		}
		return true;
	}

	bool ValidateAcyclic()
	{
		TArray<int32> Ready;
		for (int32 Index = 0; Index < Incoming.Num(); ++Index)
		{
			if (Incoming[Index] == 0)
			{
				Ready.Add(Index);
			}
		}
		for (int32 Cursor = 0; Cursor < Ready.Num(); ++Cursor)
		{
			for (int32 Target : Edges[Ready[Cursor]])
			{
				--Incoming[Target];
				if (Incoming[Target] == 0)
				{
					Ready.Add(Target);
				}
			}
		}
		if (Ready.Num() != Graph.Nodes.Num())
		{
			OutError = TEXT("Graph contains a cycle.");
			return false;
		}
		return true;
	}

	const UVoxelLandTerrainGraph& Graph;
	FString& OutError;
	TMap<FGuid, int32> NodeIndices;
	TMap<FGuid, const FVoxelLandGraphPinRecord*> Pins;
	TMap<FGuid, FGuid> PinOwners;
	TArray<TArray<int32>> Edges;
	TArray<int32> Incoming;
};
}

bool UVoxelLandTerrainGraph::ValidateStructure(FString& OutError) const
{
	OutError.Reset();
	return FGraphStructureValidator(*this, OutError).Validate();
}

bool UVoxelLandTerrainGraph::CaptureSnapshot(FVoxelLandGraphSnapshot& OutSnapshot, FString& OutError) const
{
	OutSnapshot = FVoxelLandGraphSnapshot();
	if (!IsInGameThread())
	{
		OutError = TEXT("Graph snapshot must be captured on the game thread.");
		return false;
	}
	if (!ValidateStructure(OutError))
	{
		return false;
	}
	OutSnapshot.SourceVersion = SourceVersion;
	OutSnapshot.Parameters = Parameters;
	OutSnapshot.Nodes = Nodes;
	OutSnapshot.Links = Links;
	return true;
}
