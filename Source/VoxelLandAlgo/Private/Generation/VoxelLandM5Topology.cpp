#include "Generation/VoxelLandM5Topology.h"

#include "Data/Enums/VoxelLandM5PortalAxis.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5Seed.h"

namespace
{
constexpr int32 MaximumNodes = 65536;
constexpr int32 MaximumConnections = 262144;

// Stable insertion sort is used instead of an implementation-dependent sort:
// each pass performs at most n(n-1)/2 comparisons and moves. Check cancellation
// during the inner loop, including cases with an adversarial input order.
template <typename T, typename TLess>
bool M5TopologySort(TArray<T>& Values, TLess Less, TFunctionRef<bool()> IsCancelled)
{
	for (int32 I = 1; I < Values.Num(); ++I)
	{
		T Value = MoveTemp(Values[I]);
		int32 J = I;
		while (J > 0 && Less(Value, Values[J - 1]))
		{
			if (IsCancelled())
			{
				return false;
			}
			Values[J] = MoveTemp(Values[J - 1]);
			--J;
		}
		Values[J] = MoveTemp(Value);
		if (IsCancelled())
		{
			return false;
		}
	}
	return true;
}

FVoxelLandM5Diagnostic M5TopologyError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message, bool bHasId = false, uint64 Id = 0)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("Topology");
	Result.Message = Message;
	Result.bHasFeatureId = bHasId;
	Result.FeatureId = Id;
	return Result;
}

bool M5SameRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X == B.X && A.Y == B.Y;
}

bool M5LessEndpoint(const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B)
{
	if (A.Kind != B.Kind)
	{
		return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
	}
	if (A.Region.X != B.Region.X)
	{
		return A.Region.X < B.Region.X;
	}
	if (A.Region.Y != B.Region.Y)
	{
		return A.Region.Y < B.Region.Y;
	}
	return A.Id < B.Id;
}

int32 M5FindNode(const TArray<FVoxelLandM5Endpoint>& Nodes, const FVoxelLandM5Endpoint& Node)
{
	int32 First = 0;
	int32 Last = Nodes.Num();
	while (First < Last)
	{
		const int32 Mid = First + (Last - First) / 2;
		if (M5LessEndpoint(Nodes[Mid], Node))
		{
			First = Mid + 1;
		}
		else
		{
			Last = Mid;
		}
	}
	if (First == Nodes.Num() || M5LessEndpoint(Node, Nodes[First]))
	{
		return INDEX_NONE;
	}
	return First;
}

int32 M5Root(TArray<int32>& Parent, int32 Index, TFunctionRef<bool()> IsCancelled)
{
	while (Parent[Index] != Index)
	{
		if (IsCancelled())
		{
			return INDEX_NONE;
		}
		Parent[Index] = Parent[Parent[Index]];
		Index = Parent[Index];
	}
	return Index;
}

bool M5PortalKeyEqual(const FVoxelLandM5PortalKey& A, const FVoxelLandM5PortalKey& B)
{
	return A.WorldSeed == B.WorldSeed && A.StageId == B.StageId && A.Salt == B.Salt && A.Axis == B.Axis
		&& M5SameRegion(A.RegionA, B.RegionA) && M5SameRegion(A.RegionB, B.RegionB);
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::Topology::Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	if (!Candidate->UsesBudget(Budget.Get()) || !Candidate->MatchesSource(Settings, WorldSeed, Region))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Candidate provenance or budget mismatch"));
	}
	const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
	if (!M5SameRegion(Plan.Region, Region))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Candidate region does not match provenance"));
	}
	const int64 NodeCount = static_cast<int64>(Plan.Chambers.Num()) + Plan.Entrances.Num() + Plan.Portals.Num();
	if (Settings.Limits.MaxCandidates < 1 || Settings.Limits.MaxEdges < 1 || Settings.Limits.MaxBytes < 1
		|| Settings.Limits.MaxValidationWork < 1 || Settings.Topology.EntranceCount < 0 || NodeCount > MaximumNodes
		|| Plan.Connections.Num() > MaximumConnections || Plan.Chambers.Num() > Settings.Limits.MaxCandidates
		|| Plan.Connections.Num() > Settings.Limits.MaxEdges || Plan.Entrances.Num() > Settings.Topology.EntranceCount
		|| Plan.Portals.Num() > 4)
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Topology count exceeds admission limit"));
	}
	if (!Settings.StageId.IsValid())
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Expected cave stage ID is invalid"));
	}
	if (Plan.Chambers.IsEmpty() || Plan.Connections.IsEmpty())
	{
		return M5TopologyError(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Topology requires a chamber and a route"));
	}
	int64 BudgetBytes = 0;
	int64 BudgetWork = 0;
	Budget->GetLimits(BudgetBytes, BudgetWork);
	if (BudgetBytes > Settings.Limits.MaxBytes || BudgetWork > Settings.Limits.MaxValidationWork)
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Shared budget exceeds per-plan limits"));
	}
	// Four scratch arrays: nodes, parent, connections and canonical edges. Twice
	// their payload accounts for capacity slack; 4096 further bytes cover the
	// bounded SHA packet/digest and diagnostic scratch. Unreal allocator metadata
	// is excluded, as documented by FVoxelLandM5Budget.
	using namespace VoxelLand::M5::CheckedMath;
	int64 NodeBytes = 0, EdgeBytes = 0, ScratchBytes = 0, Sum = 0, Square = 0, Work = 0, PortalWork = 0;
	if (!TryMultiplyNonNegative(NodeCount, sizeof(FVoxelLandM5Endpoint) + sizeof(int32), NodeBytes)
		|| !TryMultiplyNonNegative(Plan.Connections.Num(), sizeof(FVoxelLandM5Connection) + sizeof(uint64), EdgeBytes)
		|| !TryAdd(NodeBytes, EdgeBytes, ScratchBytes) || !TryMultiplyNonNegative(ScratchBytes, 2, ScratchBytes)
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes) || !TryAdd(NodeCount, Plan.Connections.Num(), Sum)
		|| !TryAdd(Sum, 1, Sum) || !TryMultiplyNonNegative(Sum, Sum, Square)
		|| !TryMultiplyNonNegative(Square, 16, Work) || !TryMultiplyNonNegative(Plan.Portals.Num(), 1024, PortalWork)
		|| !TryAdd(Work, PortalWork, Work) || !TryAdd(Work, 4096, Work))
	{
		return M5TopologyError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Topology admission arithmetic overflow"));
	}
	// Work units bound graph operations, not CPU instructions: each insertion
	// sort uses <= n(n-1)/2 comparisons/moves, union/find uses <= nodes per
	// edge, endpoint searches <= nodes per edge, plus linear scans. 16*(N+E+1)^2
	// bounds all of these and their cancellation checks. SHA-256 of each fixed
	// 77-byte packet needs two compression blocks, covered by 1024 per portal.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	if (!Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Topology scratch admission failed"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Topology work admission failed"));
	}
	if (IsCancelled())
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}

	TArray<FVoxelLandM5Endpoint> Nodes;
	Nodes.Reserve(static_cast<int32>(NodeCount));
	const auto AddNode = [&Nodes, &Plan](EVoxelLandM5EndpointKind Kind, uint64 Id) {
		FVoxelLandM5Endpoint Endpoint;
		Endpoint.bValid = true;
		Endpoint.Kind = Kind;
		Endpoint.Region = Plan.Region;
		Endpoint.Id = Id;
		Nodes.Add(Endpoint);
	};
	for (const FVoxelLandM5Chamber& Chamber : Plan.Chambers)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (!Chamber.bHasId)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Chamber has no ID"));
		}
		AddNode(EVoxelLandM5EndpointKind::Chamber, Chamber.Id);
	}
	for (const FVoxelLandM5Entrance& Entrance : Plan.Entrances)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (!Entrance.bHasId || !Entrance.bHasConnectionId)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Entrance lacks an ID or connection"),
				Entrance.bHasId, Entrance.Id);
		}
		AddNode(EVoxelLandM5EndpointKind::Entrance, Entrance.Id);
	}
	for (const FVoxelLandM5Portal& Portal : Plan.Portals)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		const FVoxelLandM5PortalKey& Key = Portal.Key;
		if (!Portal.bHasId || Key.WorldSeed != WorldSeed || Key.StageId != Settings.StageId || Key.Salt != Settings.Salt
			|| (!M5SameRegion(Plan.Region, Key.RegionA) && !M5SameRegion(Plan.Region, Key.RegionB)))
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid portal key, ID or owning region"),
				Portal.bHasId, Portal.Id);
		}
		// RegionA must be the lexicographically smaller adjacent region.
		if (Key.RegionA.X > Key.RegionB.X || (Key.RegionA.X == Key.RegionB.X && Key.RegionA.Y >= Key.RegionB.Y))
		{
			return M5TopologyError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Portal key is not canonical"), true, Portal.Id);
		}
		TArray<uint8> Digest;
		uint64 ExpectedId = 0;
		if (!VoxelLand::M5::Seed::TryPortalDigest(Key.WorldSeed, Key.StageId, static_cast<uint64>(Key.Salt),
				Key.RegionA.X, Key.RegionA.Y, Key.RegionB.X, Key.RegionB.Y, Digest)
			|| !VoxelLand::M5::Seed::TryDigestWord(Digest, ExpectedId))
		{
			return M5TopologyError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Portal regions are not adjacent"), true, Portal.Id);
		}
		const bool bAxisMatches = (Key.Axis == EVoxelLandM5PortalAxis::X && Key.RegionA.Y == Key.RegionB.Y)
							   || (Key.Axis == EVoxelLandM5PortalAxis::Y && Key.RegionA.X == Key.RegionB.X);
		if (!bAxisMatches || ExpectedId != Portal.Id)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput,
				TEXT("Portal axis or digest ID does not match key"), true, Portal.Id);
		}
		AddNode(EVoxelLandM5EndpointKind::Portal, Portal.Id);
	}
	if (!M5TopologySort(Nodes, M5LessEndpoint, IsCancelled))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	for (int32 I = 1; I < Nodes.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (!M5LessEndpoint(Nodes[I - 1], Nodes[I]))
		{
			if (Nodes[I].Kind == EVoxelLandM5EndpointKind::Portal)
			{
				const FVoxelLandM5Portal* First = nullptr;
				for (const FVoxelLandM5Portal& Portal : Plan.Portals)
				{
					if (IsCancelled())
					{
						return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
					}
					if (Portal.Id == Nodes[I].Id)
					{
						if (First && !M5PortalKeyEqual(First->Key, Portal.Key))
						{
							return M5TopologyError(EVoxelLandM5ErrorCode::IdentityCollision,
								TEXT("Distinct portal keys share an ID"), true, Portal.Id);
						}
						First = &Portal;
					}
				}
			}
			return M5TopologyError(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Duplicate typed node ID"), true, Nodes[I].Id);
		}
	}

	TArray<FVoxelLandM5Connection> Connections = Plan.Connections;
	for (const FVoxelLandM5Connection& Connection : Connections)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (!Connection.bHasId)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Connection has no ID"));
		}
	}
	if (!M5TopologySort(
			Connections, [](const FVoxelLandM5Connection& A, const FVoxelLandM5Connection& B) { return A.Id < B.Id; },
			IsCancelled))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	TArray<int32> Parent;
	Parent.Reserve(Nodes.Num());
	for (int32 I = 0; I < Nodes.Num(); ++I)
	{
		Parent.Add(I);
	}
	TArray<uint64> Edges;
	Edges.Reserve(Connections.Num());
	for (int32 I = 0; I < Connections.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		const FVoxelLandM5Connection& Connection = Connections[I];
		if (I > 0 && Connection.Id == Connections[I - 1].Id)
		{
			return M5TopologyError(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Duplicate connection ID"), true, Connection.Id);
		}
		if (!Connection.Start.bValid || !Connection.End.bValid || !M5SameRegion(Connection.Start.Region, Plan.Region)
			|| !M5SameRegion(Connection.End.Region, Plan.Region))
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid connection endpoint or region"),
				true, Connection.Id);
		}
		const int32 A = M5FindNode(Nodes, Connection.Start);
		const int32 B = M5FindNode(Nodes, Connection.End);
		if (A == INDEX_NONE || B == INDEX_NONE || A == B)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Connection references missing or identical nodes"), true, Connection.Id);
		}
		const uint32 Low = static_cast<uint32>(FMath::Min(A, B));
		const uint32 High = static_cast<uint32>(FMath::Max(A, B));
		Edges.Add((static_cast<uint64>(Low) << 32) | High);
		const int32 ARoot = M5Root(Parent, A, IsCancelled);
		const int32 BRoot = M5Root(Parent, B, IsCancelled);
		if (ARoot == INDEX_NONE || BRoot == INDEX_NONE)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (ARoot != BRoot)
		{
			Parent[FMath::Max(ARoot, BRoot)] = FMath::Min(ARoot, BRoot);
		}
	}
	if (!M5TopologySort(Edges, [](uint64 A, uint64 B) { return A < B; }, IsCancelled))
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	for (int32 I = 1; I < Edges.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (Edges[I] == Edges[I - 1])
		{
			return M5TopologyError(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Duplicate undirected connection"));
		}
	}
	for (const FVoxelLandM5Entrance& Entrance : Plan.Entrances)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		bool bLinked = false;
		int32 First = 0;
		int32 Last = Connections.Num();
		while (First < Last)
		{
			if (IsCancelled())
			{
				return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
			}
			const int32 Mid = First + (Last - First) / 2;
			if (Connections[Mid].Id < Entrance.ConnectionId)
			{
				First = Mid + 1;
			}
			else
			{
				Last = Mid;
			}
		}
		if (First < Connections.Num() && Connections[First].Id == Entrance.ConnectionId)
		{
			const FVoxelLandM5Connection& Connection = Connections[First];
			bLinked =
				(Connection.Start.Kind == EVoxelLandM5EndpointKind::Entrance && Connection.Start.Id == Entrance.Id)
				|| (Connection.End.Kind == EVoxelLandM5EndpointKind::Entrance && Connection.End.Id == Entrance.Id);
		}
		if (!bLinked)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Entrance connection is missing or unrelated"), true, Entrance.Id);
		}
	}
	const int32 Root = M5Root(Parent, 0, IsCancelled);
	if (Root == INDEX_NONE)
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	for (int32 I = 1; I < Parent.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		const int32 NodeRoot = M5Root(Parent, I, IsCancelled);
		if (NodeRoot == INDEX_NONE)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
		}
		if (NodeRoot != Root)
		{
			return M5TopologyError(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Topology contains disconnected nodes"), true, Nodes[I].Id);
		}
	}
	if (IsCancelled())
	{
		return M5TopologyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Topology validation cancelled"));
	}
	return FVoxelLandM5Diagnostic();
}
