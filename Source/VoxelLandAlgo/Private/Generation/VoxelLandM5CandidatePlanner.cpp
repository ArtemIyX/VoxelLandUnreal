#include "Generation/VoxelLandM5CandidatePlanner.h"

#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5RegionalSeed.h"
#include "Generation/VoxelLandM5Seed.h"

namespace
{
constexpr int64 LimitCm = 1048576;
constexpr int32 HardCandidateLimit = 256;
constexpr int32 HardEdgeLimit = 33670; // (256 + four portals) choose 2.
constexpr uint32 MaxDraws = 64;

class FCandidateNode
{
public:
	FVoxelLandM5Endpoint Endpoint;
	FVoxelLandM5PointCm Position;
};
class FCandidateEdge
{
public:
	int32 A = 0;
	int32 B = 0;
	uint64 DistanceSquared = 0;
	uint64 LoopOrder = 0;
	bool bTree = false;
	bool bLoop = false;
};

FVoxelLandM5Diagnostic Failure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.CandidatePlanner");
	Result.Message = Message;
	return Result;
}

bool PointInEnvelope(const FVoxelLandM5PointCm& P)
{
	return P.X >= -LimitCm && P.X <= LimitCm && P.Y >= -LimitCm && P.Y <= LimitCm && P.Z >= -LimitCm && P.Z <= LimitCm;
}
bool ValidBounds(const FVoxelLandM5Bounds& B)
{
	const auto MinOk = [](int64 V) {
		return V >= -LimitCm && V <= LimitCm;
	};
	const auto MaxOk = [](int64 V) {
		return V > -LimitCm && V <= LimitCm + 1;
	};
	return MinOk(B.MinInclusive.X) && MinOk(B.MinInclusive.Y) && MinOk(B.MinInclusive.Z) && MaxOk(B.MaxExclusive.X)
		&& MaxOk(B.MaxExclusive.Y) && MaxOk(B.MaxExclusive.Z) && B.MinInclusive.X < B.MaxExclusive.X
		&& B.MinInclusive.Y < B.MaxExclusive.Y && B.MinInclusive.Z < B.MaxExclusive.Z;
}
uint64 SquaredDistance(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
{
	// Validated +/-2^20 cm gives |delta| <= 2^21, so this sum is < 2^44.
	const int64 X = A.X - B.X, Y = A.Y - B.Y, Z = A.Z - B.Z;
	return uint64(X * X + Y * Y + Z * Z);
}
bool SamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}
bool LessRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X < B.X || (A.X == B.X && A.Y < B.Y);
}

bool StreamWord(const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5RegionCoord& R, uint8 Namespace,
	uint64 Id, uint32 Attempt, uint32 Draw, uint64& Out)
{
	TArray<uint8> Digest;
	const bool bOk = S.PlanningMode == EVoxelLandM5PlanningMode::Regional
					   ? VoxelLand::M5::RegionalSeed::TryStreamDigest(
							 Namespace, Seed, S.StageId, uint64(S.Salt), R.X, R.Y, Id, Attempt, Draw, Digest)
					   : VoxelLand::M5::Seed::TryStreamDigest(
							 Namespace, Seed, S.StageId, uint64(S.Salt), Id, Attempt, Draw, Digest);
	return bOk && VoxelLand::M5::Seed::TryDigestWord(Digest, Out);
}
// The full typed endpoint keys, not mutable node positions, determine an edge's
// independent loop ordering. This is scratch-only until the complete descriptor is versioned.
bool EdgeWord(const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5RegionCoord& R,
	const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B, uint32 Draw, uint64& Out)
{
	TArray<uint8> Packet;
	const bool bOk = S.PlanningMode == EVoxelLandM5PlanningMode::Regional
					   ? VoxelLand::M5::RegionalSeed::TryStreamPacket(
							 2, Seed, S.StageId, uint64(S.Salt), R.X, R.Y, 0, 0, Draw, Packet)
					   : VoxelLand::M5::Seed::TryStreamPacket(2, Seed, S.StageId, uint64(S.Salt), 0, 0, Draw, Packet);
	if (!bOk)
	{
		return false;
	}
	const auto AppendEndpoint = [&Packet](const FVoxelLandM5Endpoint& E) {
		Packet.Add(uint8(E.Kind));
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Packet.Add(uint8(E.Id >> Shift));
		}
	};
	AppendEndpoint(A);
	AppendEndpoint(B);
	TArray<uint8> Digest;
	return VoxelLand::M5::Seed::TrySha256(Packet, Digest) && VoxelLand::M5::Seed::TryDigestWord(Digest, Out);
}
bool RandomInterval(const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5RegionCoord& R, uint8 Namespace,
	uint64 Id, uint32 Attempt, uint32 FirstDraw, int64 Min, int64 Max, TFunctionRef<bool()> IsCancelled, int64& Out)
{
	uint32 Draws = 0;
	return VoxelLand::M5::Seed::TryUniformSigned(
		Min, Max, FirstDraw, MaxDraws,
		[&S, Seed, &R, Namespace, Id, Attempt](
			uint32 Draw, uint64& Word) { return StreamWord(S, Seed, R, Namespace, Id, Attempt, Draw, Word); },
		IsCancelled, Out, Draws);
}

bool RegionBounds(const FVoxelLandM5CaveSettings& S, const FVoxelLandM5RegionCoord& R, FVoxelLandM5Bounds& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	int64 X, Y, XEnd, YEnd;
	if (!TryMultiply(R.X, S.RegionSizeCm, X) || !TryAdd(X, S.RegionOriginXCm, X) || !TryMultiply(R.Y, S.RegionSizeCm, Y)
		|| !TryAdd(Y, S.RegionOriginYCm, Y) || !TryAdd(X, S.RegionSizeCm, XEnd) || !TryAdd(Y, S.RegionSizeCm, YEnd))
	{
		return false;
	}
	FVoxelLandM5Bounds B = S.Bounds;
	B.MinInclusive.X = X;
	B.MinInclusive.Y = Y;
	B.MaxExclusive.X = XEnd;
	B.MaxExclusive.Y = YEnd;
	if (!ValidBounds(B) || X < S.Bounds.MinInclusive.X || Y < S.Bounds.MinInclusive.Y || XEnd > S.Bounds.MaxExclusive.X
		|| YEnd > S.Bounds.MaxExclusive.Y)
	{
		return false;
	}
	Out = B;
	return true;
}

bool AddPortal(const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5RegionCoord& Region,
	const FVoxelLandM5RegionCoord& Neighbor, const FVoxelLandM5Bounds& Bounds, FVoxelLandM5CavePlan& Plan,
	TArray<FCandidateNode>& Nodes)
{
	FVoxelLandM5Portal Portal;
	Portal.Key.WorldSeed = Seed;
	Portal.Key.StageId = S.StageId;
	Portal.Key.Salt = S.Salt;
	Portal.Key.RegionA = LessRegion(Region, Neighbor) ? Region : Neighbor;
	Portal.Key.RegionB = LessRegion(Region, Neighbor) ? Neighbor : Region;
	const bool bX = Region.X != Neighbor.X;
	Portal.Key.Axis = bX ? EVoxelLandM5PortalAxis::X : EVoxelLandM5PortalAxis::Y;
	TArray<uint8> Digest;
	if (!VoxelLand::M5::Seed::TryPortalDigest(
			Seed, S.StageId, uint64(S.Salt), Region.X, Region.Y, Neighbor.X, Neighbor.Y, Digest)
		|| !VoxelLand::M5::Seed::TryDigestWord(Digest, Portal.Id))
	{
		return false;
	}
	for (const FVoxelLandM5Portal& Existing : Plan.Portals)
	{
		if (Existing.Id == Portal.Id)
		{
			return false;
		}
	}
	Portal.bHasId = true;
	// Both sides derive the same maximum radius; local links can be narrower.
	Portal.RadiusCm = S.Topology.MaxTunnelRadiusCm;
	Portal.CenterCm.X = bX ? (Neighbor.X > Region.X ? Bounds.MaxExclusive.X : Bounds.MinInclusive.X)
						   : Bounds.MinInclusive.X + (Bounds.MaxExclusive.X - Bounds.MinInclusive.X) / 2;
	Portal.CenterCm.Y = !bX ? (Neighbor.Y > Region.Y ? Bounds.MaxExclusive.Y : Bounds.MinInclusive.Y)
							: Bounds.MinInclusive.Y + (Bounds.MaxExclusive.Y - Bounds.MinInclusive.Y) / 2;
	Portal.CenterCm.Z = Bounds.MinInclusive.Z + (Bounds.MaxExclusive.Z - Bounds.MinInclusive.Z) / 2;
	if (!PointInEnvelope(Portal.CenterCm))
	{
		return false;
	}
	Plan.Portals.Add(Portal);
	FCandidateNode Node;
	Node.Endpoint.bValid = true;
	Node.Endpoint.Kind = EVoxelLandM5EndpointKind::Portal;
	Node.Endpoint.Region = Region;
	Node.Endpoint.Id = Portal.Id;
	Node.Position = Portal.CenterCm;
	Nodes.Add(Node);
	return true;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::CandidatePlanner::Build(const FVoxelLandM5CaveSettings& S, int64 Seed,
	const FVoxelLandM5RegionCoord& Region, TFunctionRef<bool()> IsCancelled, FVoxelLandM5CavePlan& Out)
{
	const auto Cancelled = [&IsCancelled]() {
		return IsCancelled();
	};
	if (Cancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
	}
	const auto& T = S.Topology;
	const auto& L = S.Limits;
	if (!S.bEnabled || !S.StageId.IsValid() || !ValidBounds(S.Bounds)
		|| (S.PlanningMode != EVoxelLandM5PlanningMode::Finite && S.PlanningMode != EVoxelLandM5PlanningMode::Regional)
		|| T.MinChambers < 1 || T.MaxChambers < T.MinChambers || T.MaxChambers > HardCandidateLimit
		|| L.MaxCandidates < T.MaxChambers || L.MaxCandidates > HardCandidateLimit || L.MaxEdges < 0
		|| L.MaxEdges > HardEdgeLimit || L.MaxSegments < 1 || L.MaxAttempts < 1 || L.MaxAttempts > 64 || L.MaxBytes < 1
		|| T.MinLoops < 0 || T.MaxLoops < T.MinLoops || T.MinTunnelRadiusCm < 1
		|| T.MaxTunnelRadiusCm < T.MinTunnelRadiusCm || T.MinChamberRadiusCm < T.MaxTunnelRadiusCm
		|| T.MaxChamberRadiusCm < T.MinChamberRadiusCm || T.MaxChamberRadiusCm > LimitCm)
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid cave candidate settings or budgets"));
	}
	FVoxelLandM5CavePlan Scratch;
	Scratch.Region = Region;
	if (S.PlanningMode == EVoxelLandM5PlanningMode::Regional)
	{
		if (S.RegionSizeCm <= 0 || S.RegionSizeCm > 2 * LimitCm || S.RegionOriginXCm < -LimitCm
			|| S.RegionOriginXCm > LimitCm || S.RegionOriginYCm < -LimitCm || S.RegionOriginYCm > LimitCm
			|| (S.Bounds.MinInclusive.X - S.RegionOriginXCm) % S.RegionSizeCm != 0
			|| (S.Bounds.MaxExclusive.X - S.RegionOriginXCm) % S.RegionSizeCm != 0
			|| (S.Bounds.MinInclusive.Y - S.RegionOriginYCm) % S.RegionSizeCm != 0
			|| (S.Bounds.MaxExclusive.Y - S.RegionOriginYCm) % S.RegionSizeCm != 0
			|| !RegionBounds(S, Region, Scratch.Bounds))
		{
			return Failure(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Region is outside the bounded planning domain"));
		}
	}
	else
	{
		Scratch.Bounds = S.Bounds;
	}
	const FVoxelLandM5Bounds& B = Scratch.Bounds;
	const int64 Margin = T.MaxChamberRadiusCm;
	const int64 MinX = B.MinInclusive.X + Margin, MaxX = B.MaxExclusive.X - 1 - Margin;
	const int64 MinY = B.MinInclusive.Y + Margin, MaxY = B.MaxExclusive.Y - 1 - Margin;
	const int64 MinZ = B.MinInclusive.Z + Margin, MaxZ = B.MaxExclusive.Z - 1 - Margin;
	if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ)
	{
		return Failure(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Planning volume cannot contain a chamber"));
	}
	const int32 MaxNodes = T.MaxChambers + (S.PlanningMode == EVoxelLandM5PlanningMode::Regional ? 4 : 0);
	const int64 MaxPairs = int64(MaxNodes) * (MaxNodes - 1) / 2;
	// Charge every simultaneously live payload, including selected output arrays,
	// union-find and sorting indices. Fourfold slack covers UE array capacity growth
	// and temporary sort storage; the fixed reserve covers small hash packet buffers.
	// Admission happens before any variable-size scratch allocation.
	using namespace VoxelLand::M5::CheckedMath;
	const int64 MaxSelected = FMath::Min(MaxPairs, int64(L.MaxSegments));
	int64 EdgeBytes, NodeBytes, OutputBytes, PayloadBytes, NeededBytes;
	if (!TryMultiplyNonNegative(MaxPairs, int64(sizeof(FCandidateEdge) + sizeof(int32)), EdgeBytes)
		|| !TryMultiplyNonNegative(MaxNodes,
			int64(sizeof(FCandidateNode) + sizeof(int32) + sizeof(FVoxelLandM5Chamber) + sizeof(FVoxelLandM5Portal)),
			NodeBytes)
		|| !TryMultiplyNonNegative(
			MaxSelected, int64(sizeof(FVoxelLandM5Connection) + sizeof(FVoxelLandM5Segment)), OutputBytes)
		|| !TryAdd(EdgeBytes, NodeBytes, PayloadBytes) || !TryAdd(PayloadBytes, OutputBytes, PayloadBytes)
		|| !TryMultiplyNonNegative(PayloadBytes, 4, NeededBytes) || !TryAdd(NeededBytes, 16384, NeededBytes))
	{
		return Failure(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Candidate admission size overflow"));
	}
	if (MaxPairs > L.MaxEdges || NeededBytes > L.MaxBytes)
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Candidate graph exceeds edge or memory budget"));
	}
	TArray<FCandidateNode> Nodes;
	Nodes.Reserve(MaxNodes);
	int64 Target = 0;
	if (!RandomInterval(S, Seed, Region, 1, MAX_uint64, 0, 0, T.MinChambers, T.MaxChambers, Cancelled, Target))
	{
		return Failure(Cancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Blocked,
			TEXT("Could not select chamber count"));
	}
	const uint64 Spacing = uint64(2 * Margin) * uint64(2 * Margin);
	for (int32 Id = 0; Id < L.MaxCandidates && Scratch.Chambers.Num() < Target; ++Id)
	{
		// Attempts belong to this stable candidate ID and have independent seed packets.
		for (int32 Attempt = 0; Attempt < L.MaxAttempts; ++Attempt)
		{
			if (Cancelled())
			{
				return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
			}
			FVoxelLandM5Chamber Chamber;
			int64 Radius = 0;
			if (!RandomInterval(S, Seed, Region, 1, Id, uint32(Attempt), 0, MinX, MaxX, Cancelled, Chamber.CenterCm.X)
				|| !RandomInterval(
					S, Seed, Region, 1, Id, uint32(Attempt), 64, MinY, MaxY, Cancelled, Chamber.CenterCm.Y)
				|| !RandomInterval(
					S, Seed, Region, 1, Id, uint32(Attempt), 128, MinZ, MaxZ, Cancelled, Chamber.CenterCm.Z)
				|| !RandomInterval(S, Seed, Region, 1, Id, uint32(Attempt), 192, T.MinChamberRadiusCm,
					T.MaxChamberRadiusCm, Cancelled, Radius))
			{
				return Failure(Cancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Blocked,
					TEXT("Candidate random stream exhausted"));
			}
			bool bSeparated = true;
			for (const FVoxelLandM5Chamber& Prior : Scratch.Chambers)
			{
				if (SquaredDistance(Prior.CenterCm, Chamber.CenterCm) < Spacing)
				{
					bSeparated = false;
					break;
				}
			}
			if (!bSeparated)
			{
				continue;
			}
			Chamber.bHasId = true;
			Chamber.Id = uint64(Id);
			Chamber.RadiusCm = int32(Radius);
			Scratch.Chambers.Add(Chamber);
			FCandidateNode Node;
			Node.Endpoint.bValid = true;
			Node.Endpoint.Kind = EVoxelLandM5EndpointKind::Chamber;
			Node.Endpoint.Region = Region;
			Node.Endpoint.Id = Chamber.Id;
			Node.Position = Chamber.CenterCm;
			Nodes.Add(Node);
			break;
		}
	}
	if (Scratch.Chambers.Num() != Target)
	{
		return Failure(EVoxelLandM5ErrorCode::Blocked, TEXT("Candidate spacing exhausted the bounded chamber budget"));
	}
	if (S.PlanningMode == EVoxelLandM5PlanningMode::Regional)
	{
		// A portal is required only when the neighboring full region lies inside the declared domain.
		// Its center uses the face midpoint and shared vertical interval, independent of request side.
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			for (int32 Sign = -1; Sign <= 1; Sign += 2)
			{
				FVoxelLandM5RegionCoord Neighbor = Region;
				int64 Changed;
				const int64 Current = Axis == 0 ? Region.X : Region.Y;
				if (!VoxelLand::M5::CheckedMath::TryAdd(Current, Sign, Changed))
				{
					return Failure(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Neighbor region overflow"));
				}
				if (Axis == 0)
				{
					Neighbor.X = Changed;
				}
				else
				{
					Neighbor.Y = Changed;
				}
				FVoxelLandM5Bounds NeighborBounds;
				if (!RegionBounds(S, Neighbor, NeighborBounds))
				{
					continue;
				}
				if (!AddPortal(S, Seed, Region, Neighbor, B, Scratch, Nodes))
				{
					return Failure(
						EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Cannot construct unique shared portal"));
				}
			}
		}
	}
	// Canonical node ordering makes equal-distance edge ties depend on typed IDs,
	// not portal visitation or incidental storage order.
	Nodes.Sort([](const FCandidateNode& A, const FCandidateNode& Other) {
		if (A.Endpoint.Kind != Other.Endpoint.Kind)
		{
			return uint8(A.Endpoint.Kind) < uint8(Other.Endpoint.Kind);
		}
		return A.Endpoint.Id < Other.Endpoint.Id;
	});
	TArray<FCandidateEdge> Edges;
	const int32 NodeCount = Nodes.Num();
	Edges.Reserve(NodeCount * (NodeCount - 1) / 2);
	for (int32 I = 0; I < NodeCount; ++I)
	{
		for (int32 J = I + 1; J < NodeCount; ++J)
		{
			if (Cancelled())
			{
				return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
			}
			if (SamePoint(Nodes[I].Position, Nodes[J].Position))
			{
				return Failure(EVoxelLandM5ErrorCode::Blocked, TEXT("Candidate nodes occupy the same position"));
			}
			FCandidateEdge Edge;
			Edge.A = I;
			Edge.B = J;
			Edge.DistanceSquared = SquaredDistance(Nodes[I].Position, Nodes[J].Position);
			Edges.Add(Edge);
		}
	}
	// Nodes are ordered by ascending accepted chamber ID then canonical face order;
	// indices therefore form a stable total tie order independent of allocation.
	Edges.Sort([](const FCandidateEdge& A, const FCandidateEdge& B) {
		if (A.DistanceSquared != B.DistanceSquared)
		{
			return A.DistanceSquared < B.DistanceSquared;
		}
		if (A.A != B.A)
		{
			return A.A < B.A;
		}
		return A.B < B.B;
	});
	TArray<int32> Parent;
	for (int32 I = 0; I < NodeCount; ++I)
	{
		Parent.Add(I);
	}
	int32 TreeEdges = 0;
	for (FCandidateEdge& Edge : Edges)
	{
		if (Cancelled())
		{
			return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
		}
		int32 A = Edge.A, BRoot = Edge.B;
		while (Parent[A] != A)
		{
			A = Parent[A];
		}
		while (Parent[BRoot] != BRoot)
		{
			BRoot = Parent[BRoot];
		}
		if (A != BRoot)
		{
			Parent[BRoot] = A;
			Edge.bTree = true;
			++TreeEdges;
		}
	}
	if (TreeEdges != NodeCount - 1)
	{
		return Failure(EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Cannot connect candidate graph"));
	}
	int64 LoopCount = 0;
	if (!RandomInterval(S, Seed, Region, 2, MAX_uint64, 0, 0, T.MinLoops, T.MaxLoops, Cancelled, LoopCount))
	{
		return Failure(Cancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Blocked,
			TEXT("Could not select loop count"));
	}
	if (LoopCount > Edges.Num() - TreeEdges || int64(TreeEdges) + LoopCount > L.MaxSegments)
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Loop or segment budget exceeded"));
	}
	for (FCandidateEdge& Edge : Edges)
	{
		if (Cancelled())
		{
			return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate loop ranking cancelled"));
		}
		if (!Edge.bTree
			&& !EdgeWord(S, Seed, Region, Nodes[Edge.A].Endpoint, Nodes[Edge.B].Endpoint, 0, Edge.LoopOrder))
		{
			return Failure(EVoxelLandM5ErrorCode::Blocked, TEXT("Could not derive loop order"));
		}
	}
	TArray<int32> LoopIndices;
	for (int32 I = 0; I < Edges.Num(); ++I)
	{
		if (!Edges[I].bTree)
		{
			LoopIndices.Add(I);
		}
	}
	LoopIndices.Sort([&Edges](int32 A, int32 BIndex) {
		if (Edges[A].LoopOrder != Edges[BIndex].LoopOrder)
		{
			return Edges[A].LoopOrder < Edges[BIndex].LoopOrder;
		}
		return A < BIndex;
	});
	for (int32 I = 0; I < LoopCount; ++I)
	{
		Edges[LoopIndices[I]].bTree = true;
		Edges[LoopIndices[I]].bLoop = true;
	}
	int32 Added = 0;
	for (const FCandidateEdge& Edge : Edges)
	{
		if (Edge.bTree)
		{
			if (Cancelled())
			{
				return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
			}
			FVoxelLandM5Connection Connection;
			Connection.bHasId = true;
			Connection.Id = uint64(Added);
			Connection.Start = Nodes[Edge.A].Endpoint;
			Connection.End = Nodes[Edge.B].Endpoint;
			Connection.bLoop = Edge.bLoop;
			Scratch.Connections.Add(Connection);
			FVoxelLandM5Segment Segment;
			Segment.bHasId = true;
			Segment.Id = uint64(Added);
			Segment.bHasConnectionId = true;
			Segment.ConnectionId = Connection.Id;
			Segment.StartCm = Nodes[Edge.A].Position;
			Segment.EndCm = Nodes[Edge.B].Position;
			// The segment's endpoint sphere is the shared portal primitive. Every
			// segment incident to that portal must materialize its full radius;
			// otherwise the portal metadata overstates the actual opening.
			int64 Radius = T.MaxTunnelRadiusCm;
			if (Connection.Start.Kind != EVoxelLandM5EndpointKind::Portal
				&& Connection.End.Kind != EVoxelLandM5EndpointKind::Portal)
			{
				uint32 Draws = 0;
				if (!VoxelLand::M5::Seed::TryUniformSigned(
						T.MinTunnelRadiusCm, T.MaxTunnelRadiusCm, 64, MaxDraws,
						[&S, Seed, &Region, &Nodes, &Edge](uint32 Draw, uint64& Word) {
							return EdgeWord(
								S, Seed, Region, Nodes[Edge.A].Endpoint, Nodes[Edge.B].Endpoint, Draw, Word);
						},
						Cancelled, Radius, Draws))
				{
					return Failure(Cancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Blocked,
						TEXT("Cannot choose a tunnel radius"));
				}
			}
			Segment.RadiusCm = int32(Radius);
			Scratch.Segments.Add(Segment);
			++Added;
		}
	}
	if (Cancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Candidate planning cancelled"));
	}
	Out = MoveTemp(Scratch);
	return {};
}
