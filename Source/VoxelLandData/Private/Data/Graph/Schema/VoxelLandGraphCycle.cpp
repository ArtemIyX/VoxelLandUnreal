#include "Data/Graph/Schema/VoxelLandGraphCycle.h"

class FVoxelLandGraphCycleInternals final
{
public:
	static bool GuidLess(const FGuid& A, const FGuid& B)
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
};

bool FVoxelLandGraphCycle::Find(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes,
	TConstArrayView<FVoxelLandGraphLinkRecord> Links, FGuid& OutNode, FGuid& OutPin)
{
	OutNode.Invalidate();
	OutPin.Invalidate();
	TArray<FGuid> Ordered;
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		Ordered.Add(Node.Id);
	}
	Ordered.Sort(FVoxelLandGraphCycleInternals::GuidLess);
	TMap<FGuid, int32> Indices;
	for (int32 Index = 0; Index < Ordered.Num(); ++Index)
	{
		Indices.Add(Ordered[Index], Index);
	}
	TArray<int32> SortedLinks;
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		SortedLinks.Add(Index);
	}
	SortedLinks.Sort([&Links](int32 Left, int32 Right) {
		const FVoxelLandGraphLinkRecord& A = Links[Left];
		const FVoxelLandGraphLinkRecord& B = Links[Right];
		if (A.SourceNode != B.SourceNode)
		{
			return FVoxelLandGraphCycleInternals::GuidLess(A.SourceNode, B.SourceNode);
		}
		if (A.TargetNode != B.TargetNode)
		{
			return FVoxelLandGraphCycleInternals::GuidLess(A.TargetNode, B.TargetNode);
		}
		return FVoxelLandGraphCycleInternals::GuidLess(A.TargetPin, B.TargetPin);
	});
	TArray<TArray<int32>> Edges;
	Edges.SetNum(Ordered.Num());
	for (int32 LinkIndex : SortedLinks)
	{
		const int32* Source = Indices.Find(Links[LinkIndex].SourceNode);
		const int32* Target = Indices.Find(Links[LinkIndex].TargetNode);
		if (!Source || !Target)
		{
			return false;
		}
		Edges[*Source].Add(LinkIndex);
	}
	TArray<uint8> Color;
	Color.Init(0, Ordered.Num());
	TArray<int32> Stack, Cursor;
	for (int32 Start = 0; Start < Ordered.Num(); ++Start)
	{
		if (Color[Start] != 0)
		{
			continue;
		}
		Stack.Add(Start);
		Cursor.Add(0);
		Color[Start] = 1;
		while (!Stack.IsEmpty())
		{
			const int32 Node = Stack.Last();
			if (Cursor.Last() >= Edges[Node].Num())
			{
				Color[Node] = 2;
				Stack.Pop();
				Cursor.Pop();
				continue;
			}
			const FVoxelLandGraphLinkRecord& Edge = Links[Edges[Node][Cursor.Last()++]];
			const int32 Target = Indices.FindChecked(Edge.TargetNode);
			if (Color[Target] == 1)
			{
				OutNode = Edge.TargetNode;
				OutPin = Edge.TargetPin;
				return true;
			}
			if (Color[Target] == 0)
			{
				Color[Target] = 1;
				Stack.Add(Target);
				Cursor.Add(0);
			}
		}
	}
	return false;
}