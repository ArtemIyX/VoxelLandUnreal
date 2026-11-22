#include "Graph/VoxelLandM4StageAnalysis.h"

namespace VoxelLandM4StageAnalysis
{
namespace
{
constexpr int32 M4StageMaxNodes = 4096;
constexpr int32 M4StageMaxEdges = 65536;
constexpr int32 M4StageMaxOutputs = 256;
constexpr int32 M4StageMaxOutputNameLength = 128;

FVoxelLandError StageFailure(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidProgram;
	Error.Message = Message;
	return Error;
}

bool StageGuidLess(const FGuid& Left, const FGuid& Right)
{
	if (Left.A != Right.A)
	{
		return Left.A < Right.A;
	}
	if (Left.B != Right.B)
	{
		return Left.B < Right.B;
	}
	if (Left.C != Right.C)
	{
		return Left.C < Right.C;
	}
	return Left.D < Right.D;
}

int32 StageRank(EVoxelLandM4StageKind Stage)
{
	switch (Stage)
	{
		case EVoxelLandM4StageKind::Global:
			return 0;
		case EVoxelLandM4StageKind::Sample:
			return 1;
		case EVoxelLandM4StageKind::Filter:
			return 2;
		default:
			return INDEX_NONE;
	}
}
}

FVoxelLandError CollectGlobalDependencies(TConstArrayView<FVoxelLandM4StageNode> Nodes,
	TConstArrayView<FVoxelLandM4StageOutput> Outputs, const FString& OutputName, TArray<FGuid>& OutDependencies)
{
	if (Nodes.IsEmpty() || Nodes.Num() > M4StageMaxNodes || Outputs.IsEmpty() || Outputs.Num() > M4StageMaxOutputs)
	{
		return StageFailure(TEXT("Invalid M4 stage graph size"));
	}

	TMap<FGuid, int32> IndexById;
	TArray<int32> SortedIndices;
	int64 EdgeCount = 0;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FVoxelLandM4StageNode& Node = Nodes[Index];
		if (!Node.Id.IsValid() || IndexById.Contains(Node.Id) || StageRank(Node.Stage) == INDEX_NONE)
		{
			return StageFailure(TEXT("Invalid or duplicate M4 stage node ID or stage"));
		}
		if (Node.Stage == EVoxelLandM4StageKind::Global && Node.bUsesZ)
		{
			return StageFailure(TEXT("Global M4 stage cannot use Z"));
		}
		EdgeCount += Node.Dependencies.Num();
		if (EdgeCount > M4StageMaxEdges)
		{
			return StageFailure(TEXT("M4 stage graph exceeds edge limit"));
		}
		IndexById.Add(Node.Id, Index);
		SortedIndices.Add(Index);
	}
	SortedIndices.Sort([&Nodes](int32 A, int32 B) { return StageGuidLess(Nodes[A].Id, Nodes[B].Id); });

	TArray<int32> InDegree;
	InDegree.Init(0, Nodes.Num());
	TArray<TArray<int32>> Consumers;
	Consumers.SetNum(Nodes.Num());
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FVoxelLandM4StageNode& Node = Nodes[Index];
		TSet<FGuid> Seen;
		for (const FGuid& Dependency : Node.Dependencies)
		{
			const int32* Producer = IndexById.Find(Dependency);
			if (!Producer || Seen.Contains(Dependency))
			{
				return StageFailure(TEXT("Missing or duplicate M4 stage dependency"));
			}
			Seen.Add(Dependency);
			if (StageRank(Nodes[*Producer].Stage) > StageRank(Node.Stage))
			{
				return StageFailure(TEXT("M4 stage dependency points backwards"));
			}
			++InDegree[Index];
			Consumers[*Producer].Add(Index);
		}
	}

	TArray<FString> Names;
	int32 SelectedIndex = INDEX_NONE;
	for (const FVoxelLandM4StageOutput& Output : Outputs)
	{
		const int32* Producer = IndexById.Find(Output.Producer);
		const bool bDuplicate = Names.ContainsByPredicate(
			[&Output](const FString& Name) { return Name.Equals(Output.Name, ESearchCase::CaseSensitive); });
		if (Output.Name.IsEmpty() || Output.Name.Len() > M4StageMaxOutputNameLength || bDuplicate
		    || !Output.Name.TrimStartAndEnd().Equals(Output.Name, ESearchCase::CaseSensitive) || !Producer
		    || Nodes[*Producer].Stage != EVoxelLandM4StageKind::Global)
		{
			return StageFailure(TEXT("Invalid or duplicate M4 global output"));
		}
		Names.Add(Output.Name);
		if (Output.Name.Equals(OutputName, ESearchCase::CaseSensitive))
		{
			SelectedIndex = *Producer;
		}
	}
	if (SelectedIndex == INDEX_NONE)
	{
		return StageFailure(TEXT("Requested M4 global output does not exist"));
	}

	// Kahn traversal, choosing the smallest ready GUID. Process the full graph so
	// an unreachable cycle cannot accidentally be accepted for compilation.
	TArray<int32> Order;
	Order.Reserve(Nodes.Num());
	TArray<bool> Emitted;
	Emitted.Init(false, Nodes.Num());
	while (Order.Num() < Nodes.Num())
	{
		int32 Next = INDEX_NONE;
		for (int32 Index : SortedIndices)
		{
			if (!Emitted[Index] && InDegree[Index] == 0)
			{
				Next = Index;
				break;
			}
		}
		if (Next == INDEX_NONE)
		{
			return StageFailure(TEXT("M4 stage graph contains a cycle"));
		}
		Emitted[Next] = true;
		Order.Add(Next);
		for (int32 Consumer : Consumers[Next])
		{
			--InDegree[Consumer];
		}
	}

	TArray<bool> Included;
	Included.Init(false, Nodes.Num());
	TArray<int32> Pending = {SelectedIndex};
	Included[SelectedIndex] = true;
	while (!Pending.IsEmpty())
	{
		const int32 Index = Pending.Pop(EAllowShrinking::No);
		for (const FGuid& Dependency : Nodes[Index].Dependencies)
		{
			const int32 Producer = IndexById.FindChecked(Dependency);
			if (!Included[Producer])
			{
				Included[Producer] = true;
				Pending.Add(Producer);
			}
		}
	}
	TArray<FGuid> Result;
	for (int32 Index : Order)
	{
		if (Included[Index])
		{
			Result.Add(Nodes[Index].Id);
		}
	}
	OutDependencies = MoveTemp(Result);
	return {};
}
}