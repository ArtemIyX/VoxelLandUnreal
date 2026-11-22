#include "Generation/VoxelLandM5RouteTopology.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5Seed.h"

class FVoxelLandM5RouteTopologyInternals final
{
public:
	static FVoxelLandM5Diagnostic Failure(
		EVoxelLandM5ErrorCode Code, const TCHAR* Message, bool bHasId = false, uint64 Id = 0)
	{
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("RouteTopology");
		D.Message = Message;
		D.bHasFeatureId = bHasId;
		D.FeatureId = Id;
		return D;
	}
	static bool SameRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
	{
		return A.X == B.X && A.Y == B.Y;
	}
	static bool SamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}
	static bool SameRef(const FVoxelLandM5RouteFeatureRef& A, const FVoxelLandM5RouteFeatureRef& B)
	{
		return A.bValid && B.bValid && A.Kind == B.Kind && A.Id == B.Id && SameRegion(A.Region, B.Region);
	}
	static bool SameNode(const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B)
	{
		return A.bValid && B.bValid && A.Kind == B.Kind && A.Id == B.Id && SameRegion(A.Region, B.Region);
	}
	static bool FeatureIsNode(const FVoxelLandM5RouteFeatureRef& Ref, const FVoxelLandM5Endpoint& Endpoint)
	{
		if (!Ref.bValid || !Endpoint.bValid || Ref.Id != Endpoint.Id || !SameRegion(Ref.Region, Endpoint.Region))
		{
			return false;
		}
		return (Ref.Kind == EVoxelLandM5RouteFeatureKind::Chamber && Endpoint.Kind == EVoxelLandM5EndpointKind::Chamber)
			|| (Ref.Kind == EVoxelLandM5RouteFeatureKind::Entrance
				&& Endpoint.Kind == EVoxelLandM5EndpointKind::Entrance)
			|| (Ref.Kind == EVoxelLandM5RouteFeatureKind::Portal && Endpoint.Kind == EVoxelLandM5EndpointKind::Portal)
			|| (Ref.Kind == EVoxelLandM5RouteFeatureKind::Junction
				&& Endpoint.Kind == EVoxelLandM5EndpointKind::Junction);
	}

	// Logical coordinates are bounded independently of render origin. Full voxel
	// size/grid compatibility is verified by M5 world-identity validation; this
	// function does not receive an M4 voxel size and cannot replace that check.
	static bool RegionBoundsForPortal(
		const FVoxelLandM5CaveSettings& Settings, const FVoxelLandM5RegionCoord& Region, FVoxelLandM5Bounds& Out)
	{
		using namespace VoxelLand::M5::CheckedMath;
		constexpr int64 Limit = 1048576;
		const auto& Domain = Settings.Bounds;
		if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Regional || Settings.RegionSizeCm <= 0
			|| Settings.RegionSizeCm > 2 * Limit || Settings.RegionOriginXCm < -Limit
			|| Settings.RegionOriginXCm > Limit || Settings.RegionOriginYCm < -Limit || Settings.RegionOriginYCm > Limit
			|| Domain.MinInclusive.X < -Limit || Domain.MinInclusive.Y < -Limit || Domain.MinInclusive.Z < -Limit
			|| Domain.MaxExclusive.X > Limit + 1 || Domain.MaxExclusive.Y > Limit + 1
			|| Domain.MaxExclusive.Z > Limit + 1 || Domain.MinInclusive.X >= Domain.MaxExclusive.X
			|| Domain.MinInclusive.Y >= Domain.MaxExclusive.Y || Domain.MinInclusive.Z >= Domain.MaxExclusive.Z)
		{
			return false;
		}
		int64 X, Y, XEnd, YEnd, MinXOffset, MaxXOffset, MinYOffset, MaxYOffset;
		if (!TrySubtract(Domain.MinInclusive.X, Settings.RegionOriginXCm, MinXOffset)
			|| !TrySubtract(Domain.MaxExclusive.X, Settings.RegionOriginXCm, MaxXOffset)
			|| !TrySubtract(Domain.MinInclusive.Y, Settings.RegionOriginYCm, MinYOffset)
			|| !TrySubtract(Domain.MaxExclusive.Y, Settings.RegionOriginYCm, MaxYOffset)
			|| MinXOffset % Settings.RegionSizeCm != 0 || MaxXOffset % Settings.RegionSizeCm != 0
			|| MinYOffset % Settings.RegionSizeCm != 0 || MaxYOffset % Settings.RegionSizeCm != 0
			|| !TryMultiply(Region.X, Settings.RegionSizeCm, X) || !TryAdd(X, Settings.RegionOriginXCm, X)
			|| !TryMultiply(Region.Y, Settings.RegionSizeCm, Y) || !TryAdd(Y, Settings.RegionOriginYCm, Y)
			|| !TryAdd(X, Settings.RegionSizeCm, XEnd) || !TryAdd(Y, Settings.RegionSizeCm, YEnd)
			|| X < Domain.MinInclusive.X || XEnd > Domain.MaxExclusive.X || Y < Domain.MinInclusive.Y
			|| YEnd > Domain.MaxExclusive.Y)
		{
			return false;
		}
		FVoxelLandM5Bounds Bounds = Domain;
		Bounds.MinInclusive.X = X;
		Bounds.MinInclusive.Y = Y;
		Bounds.MaxExclusive.X = XEnd;
		Bounds.MaxExclusive.Y = YEnd;
		Out = Bounds;
		return true;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::RouteTopology::ValidateStructure(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5RegionCoord& Region,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
	}
	if (!Candidate->UsesBudget(Budget.Get()) || !Candidate->MatchesSource(Settings, WorldSeed, Region))
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Candidate source or budget mismatch"));
	}
	const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
	const FVoxelLandM5ResourceLimits& L = Settings.Limits;
	const auto& Topology = Settings.Topology;
	int64 BudgetBytes = 0, BudgetWork = 0;
	Budget->GetLimits(BudgetBytes, BudgetWork);
	if (!Settings.StageId.IsValid() || !FVoxelLandM5RouteTopologyInternals::SameRegion(Plan.Region, Region)
		|| L.MaxBytes < 1 || L.MaxValidationWork < 1 || BudgetBytes > L.MaxBytes || BudgetWork > L.MaxValidationWork
		|| L.MaxCandidates < 1 || L.MaxEdges < 1 || L.MaxSegments < 1 || L.MaxJunctions < 1 || L.MaxRoutes < 1
		|| L.MaxRouteLegs < 1 || L.MaxRouteFeatureRefs < 1)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid source or limits"));
	}
	if (Topology.MinChambers < 1 || Topology.MaxChambers < Topology.MinChambers
		|| Topology.MaxChambers > L.MaxCandidates || Topology.EntranceCount < 0 || Topology.MinLoops < 0
		|| Topology.MaxLoops < Topology.MinLoops)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid chamber, entrance or loop settings"));
	}
	if (Plan.Chambers.Num() > L.MaxCandidates || Plan.Connections.Num() > L.MaxEdges
		|| Plan.Segments.Num() > L.MaxSegments || Plan.Junctions.Num() > L.MaxJunctions
		|| Plan.Routes.Num() > L.MaxRoutes || Plan.Portals.Num() > 4)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route plan count exceeds limit"));
	}
	if (Plan.Chambers.Num() < Topology.MinChambers || Plan.Chambers.Num() > Topology.MaxChambers
		|| Plan.Entrances.Num() != Topology.EntranceCount)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Chamber or entrance count does not match settings"));
	}
	if (Plan.Chambers.IsEmpty() || Plan.Connections.IsEmpty() || Plan.Routes.IsEmpty())
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Required graph or routes are empty"));
	}
	using namespace VoxelLand::M5::CheckedMath;
	// Admit work before traversing the caller-owned nested route arrays.
	if (!Budget->TryChargeWork(static_cast<int64>(Plan.Routes.Num()) + 1))
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route count preflight admission failed"));
	}
	int64 Legs = 0, Refs = 0, Items = 0, Work = 0, PortalsWork = 0, ScratchBytes = 0;
	for (const FVoxelLandM5Route& Route : Plan.Routes)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
		}
		if (!TryAdd(Legs, Route.Legs.Num(), Legs) || !TryAdd(Refs, Route.CoveredFeatures.Num(), Refs))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Route counts overflow"));
		}
		if (Legs > L.MaxRouteLegs || Refs > L.MaxRouteFeatureRefs)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Nested route count exceeds limit"));
		}
	}
	// All scans below are bounded by the square of the total input count; 64
	// accounts for nested comparisons and cancellation checks per pair. Packet
	// hashes have an additional fixed charge. Borrowed plan storage is retained
	// by Candidate's originating lease, and digest scratch is reserved here.
	Items = static_cast<int64>(Plan.Chambers.Num()) + Plan.Connections.Num() + Plan.Segments.Num()
		  + Plan.Entrances.Num() + Plan.Junctions.Num() + Plan.Portals.Num() + Plan.Routes.Num() + Legs + Refs + 1;
	if (!TryMultiplyNonNegative(Items, Items, Work) || !TryMultiplyNonNegative(Work, 64, Work)
		|| !TryMultiplyNonNegative(Plan.Portals.Num(), 1024, PortalsWork) || !TryAdd(Work, PortalsWork, Work)
		|| !TryAdd(Work, 4096, Work) || !TryMultiplyNonNegative(Plan.Routes.Num(), 16, ScratchBytes)
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes))
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Route work overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Scratch;
	if (!Budget->TryReserveBytes(ScratchBytes, Scratch) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route validation admission failed"));
	}

	// This local lookup deliberately treats namespaces separately. Duplicate IDs
	// cause failure, including zero IDs, which remain valid when bHasId is set.
	const auto FindFeature = [&Plan, &Region, &IsCancelled](const FVoxelLandM5RouteFeatureRef& Ref) -> int32 {
		if (!Ref.bValid || !FVoxelLandM5RouteTopologyInternals::SameRegion(Ref.Region, Region))
		{
			return 0;
		}
		int32 Count = 0;
		switch (Ref.Kind)
		{
			case EVoxelLandM5RouteFeatureKind::Chamber:
				for (const auto& V : Plan.Chambers)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			case EVoxelLandM5RouteFeatureKind::Connection:
				for (const auto& V : Plan.Connections)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			case EVoxelLandM5RouteFeatureKind::Segment:
				for (const auto& V : Plan.Segments)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			case EVoxelLandM5RouteFeatureKind::Entrance:
				for (const auto& V : Plan.Entrances)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			case EVoxelLandM5RouteFeatureKind::Portal:
				for (const auto& V : Plan.Portals)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			case EVoxelLandM5RouteFeatureKind::Junction:
				for (const auto& V : Plan.Junctions)
				{
					if (IsCancelled())
					{
						return -1;
					}
					if (V.bHasId && V.Id == Ref.Id)
					{
						++Count;
					}
				}
				break;
			default:
				return 0;
		}
		return Count;
	};
	bool bAnchorCancelled = false;
	const auto Anchor = [&Plan, &Region, &IsCancelled, &bAnchorCancelled](
							const FVoxelLandM5Endpoint& E, FVoxelLandM5PointCm& Out) -> bool {
		if (!E.bValid || !FVoxelLandM5RouteTopologyInternals::SameRegion(E.Region, Region))
		{
			return false;
		}
		switch (E.Kind)
		{
			case EVoxelLandM5EndpointKind::Chamber:
				for (const auto& C : Plan.Chambers)
				{
					if (IsCancelled())
					{
						bAnchorCancelled = true;
						return false;
					}
					if (C.bHasId && C.Id == E.Id && E.AnchorRole == EVoxelLandM5AnchorRole::ChamberFeet
						&& C.bHasFeetAnchor)
					{
						Out = C.FeetAnchorCm;
						return true;
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Entrance:
				for (const auto& V : Plan.Entrances)
				{
					if (IsCancelled())
					{
						bAnchorCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id)
					{
						if (E.AnchorRole == EVoxelLandM5AnchorRole::EntranceInsideFeet && V.bHasInsideFeetAnchor)
						{
							Out = V.InsideFeetAnchorCm;
							return true;
						}
						if (E.AnchorRole == EVoxelLandM5AnchorRole::EntranceOutsideFeet && V.bHasOutsideFeetAnchor)
						{
							Out = V.OutsideFeetAnchorCm;
							return true;
						}
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Portal:
				for (const auto& V : Plan.Portals)
				{
					if (IsCancelled())
					{
						bAnchorCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id)
					{
						if (E.AnchorRole == EVoxelLandM5AnchorRole::PortalRegionAFeet
							&& FVoxelLandM5RouteTopologyInternals::SameRegion(Region, V.Key.RegionA)
							&& V.bHasRegionAFeetAnchor)
						{
							Out = V.RegionAFeetAnchorCm;
							return true;
						}
						if (E.AnchorRole == EVoxelLandM5AnchorRole::PortalRegionBFeet
							&& FVoxelLandM5RouteTopologyInternals::SameRegion(Region, V.Key.RegionB)
							&& V.bHasRegionBFeetAnchor)
						{
							Out = V.RegionBFeetAnchorCm;
							return true;
						}
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Junction:
				for (const auto& V : Plan.Junctions)
				{
					if (IsCancelled())
					{
						bAnchorCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id && E.AnchorRole == EVoxelLandM5AnchorRole::JunctionFeet
						&& V.bHasFeetAnchor)
					{
						Out = V.FeetAnchorCm;
						return true;
					}
				}
				break;
			default:
				break;
		}
		return false;
	};
	for (const auto& Portal : Plan.Portals)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
		}
		TArray<uint8> Digest;
		uint64 Id = 0;
		const auto& K = Portal.Key;
		if (!Portal.bHasId || K.WorldSeed != WorldSeed || K.StageId != Settings.StageId || K.Salt != Settings.Salt
			|| (!FVoxelLandM5RouteTopologyInternals::SameRegion(Region, K.RegionA)
				&& !FVoxelLandM5RouteTopologyInternals::SameRegion(Region, K.RegionB))
			|| !(K.RegionA.X < K.RegionB.X || (K.RegionA.X == K.RegionB.X && K.RegionA.Y < K.RegionB.Y))
			|| !((K.Axis == EVoxelLandM5PortalAxis::X && K.RegionA.Y == K.RegionB.Y)
				 || (K.Axis == EVoxelLandM5PortalAxis::Y && K.RegionA.X == K.RegionB.X))
			|| !VoxelLand::M5::Seed::TryPortalDigest(K.WorldSeed, K.StageId, static_cast<uint64>(K.Salt), K.RegionA.X,
				K.RegionA.Y, K.RegionB.X, K.RegionB.Y, Digest)
			|| !VoxelLand::M5::Seed::TryDigestWord(Digest, Id) || Id != Portal.Id)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid full portal key"), Portal.bHasId, Portal.Id);
		}
		FVoxelLandM5Bounds A, B;
		if (!FVoxelLandM5RouteTopologyInternals::RegionBoundsForPortal(Settings, K.RegionA, A)
			|| !FVoxelLandM5RouteTopologyInternals::RegionBoundsForPortal(Settings, K.RegionB, B))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Portal regions exceed the supported grid"), true, Portal.Id);
		}
		const FVoxelLandM5Bounds& Own = FVoxelLandM5RouteTopologyInternals::SameRegion(Region, K.RegionA) ? A : B;
		if (!FVoxelLandM5RouteTopologyInternals::SamePoint(Plan.Bounds.MinInclusive, Own.MinInclusive)
			|| !FVoxelLandM5RouteTopologyInternals::SamePoint(Plan.Bounds.MaxExclusive, Own.MaxExclusive))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Plan bounds do not match its regional grid"), true, Portal.Id);
		}
		const int64 Radius = Portal.RadiusCm;
		const auto& P = Portal.CenterCm;
		int64 XMin, XMax, YMin, YMax, ZMin, ZMax;
		if (Radius <= 0 || Radius < Settings.Topology.MinTunnelRadiusCm || Radius > Settings.Topology.MaxTunnelRadiusCm
			|| !TrySubtract(P.X, Radius, XMin) || !TryAdd(P.X, Radius, XMax) || !TrySubtract(P.Y, Radius, YMin)
			|| !TryAdd(P.Y, Radius, YMax) || !TrySubtract(P.Z, Radius, ZMin) || !TryAdd(P.Z, Radius, ZMax))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid portal radius or extent"), true, Portal.Id);
		}
		const auto& Domain = Settings.Bounds;
		if (XMin < Domain.MinInclusive.X || XMax >= Domain.MaxExclusive.X || YMin < Domain.MinInclusive.Y
			|| YMax >= Domain.MaxExclusive.Y || ZMin < Domain.MinInclusive.Z || ZMax >= Domain.MaxExclusive.Z
			|| (K.Axis == EVoxelLandM5PortalAxis::X ? (A.MaxExclusive.X != B.MinInclusive.X || P.X != A.MaxExclusive.X
														  || YMin < A.MinInclusive.Y || YMax >= A.MaxExclusive.Y)
													: (A.MaxExclusive.Y != B.MinInclusive.Y || P.Y != A.MaxExclusive.Y
														  || XMin < A.MinInclusive.X || XMax >= A.MaxExclusive.X)))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::OutOfDomain,
				TEXT("Portal geometry does not fit the shared face"), true, Portal.Id);
		}
	}
	int64 MarkedLoops = 0;
	for (int32 ConnectionIndex = 0; ConnectionIndex < Plan.Connections.Num(); ++ConnectionIndex)
	{
		const auto& Connection = Plan.Connections[ConnectionIndex];
		if (IsCancelled())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
		}
		if (!Connection.bHasId || !Connection.Start.bValid || !Connection.End.bValid
			|| Connection.Start.AnchorRole != EVoxelLandM5AnchorRole::None
			|| Connection.End.AnchorRole != EVoxelLandM5AnchorRole::None
			|| !FVoxelLandM5RouteTopologyInternals::SameRegion(Connection.Start.Region, Region)
			|| !FVoxelLandM5RouteTopologyInternals::SameRegion(Connection.End.Region, Region)
			|| FVoxelLandM5RouteTopologyInternals::SameNode(Connection.Start, Connection.End))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Invalid graph connection"), Connection.bHasId, Connection.Id);
		}
		MarkedLoops += Connection.bLoop ? 1 : 0;
		for (int32 Previous = 0; Previous < ConnectionIndex; ++Previous)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			const auto& Other = Plan.Connections[Previous];
			if (Other.bHasId
				&& (Other.Id == Connection.Id
					|| ((FVoxelLandM5RouteTopologyInternals::SameNode(Other.Start, Connection.Start)
							&& FVoxelLandM5RouteTopologyInternals::SameNode(Other.End, Connection.End))
						|| (FVoxelLandM5RouteTopologyInternals::SameNode(Other.Start, Connection.End)
							&& FVoxelLandM5RouteTopologyInternals::SameNode(Other.End, Connection.Start)))))
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
					TEXT("Duplicate graph connection"), true, Connection.Id);
			}
		}
	}
	const int64 NodeCount =
		static_cast<int64>(Plan.Chambers.Num()) + Plan.Entrances.Num() + Plan.Portals.Num() + Plan.Junctions.Num();
	const int64 GraphLoops = static_cast<int64>(Plan.Connections.Num()) - NodeCount + 1;
	if (GraphLoops < Topology.MinLoops || GraphLoops > Topology.MaxLoops || GraphLoops != MarkedLoops)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Graph loop count or markers do not match settings"));
	}
	for (const auto& Segment : Plan.Segments)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
		}
		int32 Matches = 0;
		for (const auto& Connection : Plan.Connections)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Connection.bHasId && Connection.Id == Segment.ConnectionId)
			{
				++Matches;
			}
		}
		if (!Segment.bHasId || !Segment.bHasConnectionId || Matches != 1 || Segment.RadiusCm <= 0
			|| FVoxelLandM5RouteTopologyInternals::SamePoint(Segment.StartCm, Segment.EndCm))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Invalid segment connection or geometry"), Segment.bHasId, Segment.Id);
		}
	}
	// A graph edge must own one non-branching, continuous chain. Its geometric
	// terminal points bind to chamber/portal/junction centers or the entrance's
	// opening volume. Feet anchors are checked below;
	// they are intentionally not conflated with cave-volume centerlines.
	bool bGeometryCancelled = false;
	const auto MatchesGeometryNode = [&Plan, &IsCancelled, &bGeometryCancelled](
										 const FVoxelLandM5Endpoint& E, const FVoxelLandM5PointCm& P) {
		switch (E.Kind)
		{
			case EVoxelLandM5EndpointKind::Chamber:
				for (const auto& V : Plan.Chambers)
				{
					if (IsCancelled())
					{
						bGeometryCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id && FVoxelLandM5RouteTopologyInternals::SamePoint(V.CenterCm, P))
					{
						return true;
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Portal:
				for (const auto& V : Plan.Portals)
				{
					if (IsCancelled())
					{
						bGeometryCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id && FVoxelLandM5RouteTopologyInternals::SamePoint(V.CenterCm, P))
					{
						return true;
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Junction:
				for (const auto& V : Plan.Junctions)
				{
					if (IsCancelled())
					{
						bGeometryCancelled = true;
						return false;
					}
					if (V.bHasId && V.Id == E.Id && V.bHasCenter
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(V.CenterCm, P))
					{
						return true;
					}
				}
				break;
			case EVoxelLandM5EndpointKind::Entrance:
				for (const auto& V : Plan.Entrances)
				{
					if (IsCancelled())
					{
						bGeometryCancelled = true;
						return false;
					}
					const auto& B = V.OpeningBoundsCm;
					if (V.bHasId && V.Id == E.Id && P.X >= B.MinInclusive.X && P.X < B.MaxExclusive.X
						&& P.Y >= B.MinInclusive.Y && P.Y < B.MaxExclusive.Y && P.Z >= B.MinInclusive.Z
						&& P.Z < B.MaxExclusive.Z)
					{
						return true;
					}
				}
				break;
			default:
				break;
		}
		return false;
	};
	for (const auto& Connection : Plan.Connections)
	{
		int32 Count = 0, Terminals = 0;
		FVoxelLandM5PointCm First, Last;
		for (const auto& Segment : Plan.Segments)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Segment.ConnectionId != Connection.Id)
			{
				continue;
			}
			++Count;
			for (int32 End = 0; End < 2; ++End)
			{
				const auto& P = End == 0 ? Segment.StartCm : Segment.EndCm;
				int32 Degree = 0;
				for (const auto& Other : Plan.Segments)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
					}
					if (Other.ConnectionId == Connection.Id)
					{
						Degree += int32(FVoxelLandM5RouteTopologyInternals::SamePoint(P, Other.StartCm))
								+ int32(FVoxelLandM5RouteTopologyInternals::SamePoint(P, Other.EndCm));
					}
				}
				if (Degree > 2)
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
						TEXT("Branched segment chain"), true, Connection.Id);
				}
				if (Degree == 1)
				{
					if (Terminals++ == 0)
					{
						First = P;
					}
					else
					{
						Last = P;
					}
				}
			}
		}
		if (Count == 0 || Terminals != 2)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Connection has no single segment chain"), true, Connection.Id);
		}
		if (!((MatchesGeometryNode(Connection.Start, First) && MatchesGeometryNode(Connection.End, Last))
				|| (MatchesGeometryNode(Connection.Start, Last) && MatchesGeometryNode(Connection.End, First))))
		{
			if (bGeometryCancelled)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Segment terminals do not match graph nodes"), true, Connection.Id);
		}
		int32 Visited = 0;
		bool bHavePrevious = false;
		uint64 Previous = 0;
		FVoxelLandM5PointCm Cursor = First;
		while (Visited < Count)
		{
			const FVoxelLandM5Segment* Next = nullptr;
			for (const auto& Segment : Plan.Segments)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
				}
				if (Segment.ConnectionId == Connection.Id && (!bHavePrevious || Segment.Id != Previous)
					&& (FVoxelLandM5RouteTopologyInternals::SamePoint(Cursor, Segment.StartCm)
						|| FVoxelLandM5RouteTopologyInternals::SamePoint(Cursor, Segment.EndCm)))
				{
					if (Next)
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
							TEXT("Ambiguous segment chain"), true, Connection.Id);
					}
					Next = &Segment;
				}
			}
			if (!Next)
			{
				break;
			}
			Cursor = FVoxelLandM5RouteTopologyInternals::SamePoint(Cursor, Next->StartCm) ? Next->EndCm : Next->StartCm;
			Previous = Next->Id;
			bHavePrevious = true;
			++Visited;
		}
		if (Visited != Count || !FVoxelLandM5RouteTopologyInternals::SamePoint(Cursor, Last))
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Disconnected segment chain"), true, Connection.Id);
		}
	}
	for (const auto& Entrance : Plan.Entrances)
	{
		int32 Matches = 0;
		for (const auto& Connection : Plan.Connections)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Connection.bHasId && Connection.Id == Entrance.ConnectionId
				&& ((Connection.Start.Kind == EVoxelLandM5EndpointKind::Entrance && Connection.Start.Id == Entrance.Id)
					|| (Connection.End.Kind == EVoxelLandM5EndpointKind::Entrance && Connection.End.Id == Entrance.Id)))
			{
				++Matches;
			}
		}
		if (!Entrance.bHasId || !Entrance.bHasConnectionId || Matches != 1 || !Entrance.bHasInsideFeetAnchor
			|| !Entrance.bHasOutsideFeetAnchor)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Entrance connection is missing or unrelated"), Entrance.bHasId, Entrance.Id);
		}
	}
	for (const auto& Route : Plan.Routes)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
		}
		if (!Route.bHasId || Route.Legs.IsEmpty() || Route.CoveredFeatures.IsEmpty())
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Route is incomplete"), Route.bHasId, Route.Id);
		}
		int32 RouteIds = 0;
		for (const auto& Other : Plan.Routes)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Other.bHasId && Other.Id == Route.Id)
			{
				++RouteIds;
			}
		}
		if (RouteIds != 1)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Duplicate route ID"), true, Route.Id);
		}
		FVoxelLandM5PointCm First, Last;
		if (!Anchor(Route.Start, First) || !Anchor(Route.End, Last)
			|| !FVoxelLandM5RouteTopologyInternals::SamePoint(First, Route.Legs[0].StartFeetCm)
			|| !FVoxelLandM5RouteTopologyInternals::SamePoint(Last, Route.Legs.Last().EndFeetCm))
		{
			if (bAnchorCancelled)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Route endpoint or anchor mismatch"), true, Route.Id);
		}
		int32 Connections = 0;
		for (const auto& Ref : Route.CoveredFeatures)
		{
			const int32 Matches = FindFeature(Ref);
			if (Matches < 0)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Matches != 1)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
					TEXT("Missing or duplicate feature"), Ref.bValid, Ref.Id);
			}
			int32 Owners = 0;
			for (const auto& Other : Plan.Routes)
			{
				for (const auto& R : Other.CoveredFeatures)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
					}
					if (FVoxelLandM5RouteTopologyInternals::SameRef(Ref, R))
					{
						++Owners;
					}
				}
			}
			if (Owners != 1)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
					TEXT("Feature has multiple coverage owners"), true, Ref.Id);
			}
			// Nodes are covered by anchored endpoints; connections by their
			// checked chains. Only segment coverage needs direct leg references.
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Segment)
			{
				bool bReferenced = false;
				for (const auto& Leg : Route.Legs)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
					}
					bReferenced |= FVoxelLandM5RouteTopologyInternals::SameRef(Ref, Leg.Feature);
				}
				if (!bReferenced)
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
						TEXT("Covered segment has no route leg"), true, Ref.Id);
				}
			}
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Entrance)
			{
				for (const auto& Entrance : Plan.Entrances)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
					}
					if (!Entrance.bHasId || Entrance.Id != Ref.Id)
					{
						continue;
					}
					bool bInside = false, bOutside = false;
					for (const auto& Leg : Route.Legs)
					{
						if (IsCancelled())
						{
							return FVoxelLandM5RouteTopologyInternals::Failure(
								EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
						}
						bInside |=
							FVoxelLandM5RouteTopologyInternals::SamePoint(Leg.StartFeetCm, Entrance.InsideFeetAnchorCm)
							|| FVoxelLandM5RouteTopologyInternals::SamePoint(
								Leg.EndFeetCm, Entrance.InsideFeetAnchorCm);
						bOutside |=
							FVoxelLandM5RouteTopologyInternals::SamePoint(Leg.StartFeetCm, Entrance.OutsideFeetAnchorCm)
							|| FVoxelLandM5RouteTopologyInternals::SamePoint(
								Leg.EndFeetCm, Entrance.OutsideFeetAnchorCm);
					}
					if (!bInside || !bOutside)
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
							TEXT("Entrance route does not traverse both anchors"), true, Entrance.Id);
					}
				}
			}
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Chamber || Ref.Kind == EVoxelLandM5RouteFeatureKind::Entrance
				|| Ref.Kind == EVoxelLandM5RouteFeatureKind::Portal
				|| Ref.Kind == EVoxelLandM5RouteFeatureKind::Junction)
			{
				if (!FVoxelLandM5RouteTopologyInternals::FeatureIsNode(Ref, Route.Start)
					&& !FVoxelLandM5RouteTopologyInternals::FeatureIsNode(Ref, Route.End))
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
						TEXT("Covered node is not a route endpoint"), true, Ref.Id);
				}
			}
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Connection)
			{
				++Connections;
				for (const auto& C : Plan.Connections)
				{
					if (C.bHasId && C.Id == Ref.Id)
					{
						if (!((FVoxelLandM5RouteTopologyInternals::SameNode(C.Start, Route.Start)
								  && FVoxelLandM5RouteTopologyInternals::SameNode(C.End, Route.End))
								|| (FVoxelLandM5RouteTopologyInternals::SameNode(C.Start, Route.End)
									&& FVoxelLandM5RouteTopologyInternals::SameNode(C.End, Route.Start))))
						{
							return FVoxelLandM5RouteTopologyInternals::Failure(
								EVoxelLandM5ErrorCode::InconsistentTopology,
								TEXT("Route does not join its connection endpoints"), true, Ref.Id);
						}
					}
				}
			}
		}
		if (Connections != 1)
		{
			return FVoxelLandM5RouteTopologyInternals::Failure(
				EVoxelLandM5ErrorCode::Unproven, TEXT("A route must cover exactly one connection"), true, Route.Id);
		}
		uint64 ConnectionId = 0;
		for (const auto& Ref : Route.CoveredFeatures)
		{
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Connection)
			{
				ConnectionId = Ref.Id;
			}
		}
		for (const auto& Ref : Route.CoveredFeatures)
		{
			if (Ref.Kind == EVoxelLandM5RouteFeatureKind::Segment)
			{
				for (const auto& Segment : Plan.Segments)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
					}
					if (Segment.bHasId && Segment.Id == Ref.Id && Segment.ConnectionId != ConnectionId)
					{
						return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
							TEXT("Segment belongs to another connection"), true, Ref.Id);
					}
				}
			}
		}
		for (int32 I = 0; I < Route.Legs.Num(); ++I)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			const auto& Leg = Route.Legs[I];
			if (Leg.Feature.Kind != EVoxelLandM5RouteFeatureKind::Segment
				|| (Leg.Transition != EVoxelLandM5RouteTransition::Walk
					&& Leg.Transition != EVoxelLandM5RouteTransition::Step)
				|| FVoxelLandM5RouteTopologyInternals::SamePoint(Leg.StartFeetCm, Leg.EndFeetCm)
				|| (I && !FVoxelLandM5RouteTopologyInternals::SamePoint(Route.Legs[I - 1].EndFeetCm, Leg.StartFeetCm)))
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
					TEXT("Invalid or discontinuous route leg"), true, Route.Id);
			}
			bool bCovered = false;
			for (const auto& Ref : Route.CoveredFeatures)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
				}
				bCovered |= FVoxelLandM5RouteTopologyInternals::SameRef(Ref, Leg.Feature);
			}
			if (!bCovered)
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology,
					TEXT("Leg references uncovered feature"), true, Route.Id);
			}
		}
	}
	const auto CheckRequired = [&Plan, &Region, &IsCancelled](
								   EVoxelLandM5RouteFeatureKind Kind, bool bHasId, uint64 Id) -> int32 {
		if (!bHasId)
		{
			return 0;
		}
		FVoxelLandM5RouteFeatureRef Wanted;
		Wanted.bValid = true;
		Wanted.Kind = Kind;
		Wanted.Region = Region;
		Wanted.Id = Id;
		int32 Count = 0;
		for (const auto& R : Plan.Routes)
		{
			for (const auto& F : R.CoveredFeatures)
			{
				if (IsCancelled())
				{
					return -1;
				}
				if (FVoxelLandM5RouteTopologyInternals::SameRef(F, Wanted))
				{
					++Count;
				}
			}
		}
		return Count;
	};
#define M5_REQUIRE_FEATURE(Array, Kind)                                                                     \
	for (const auto& V : Plan.Array)                                                                        \
	{                                                                                                       \
		const int32 C = CheckRequired(EVoxelLandM5RouteFeatureKind::Kind, V.bHasId, V.Id);                  \
		if (C < 0)                                                                                          \
			return FVoxelLandM5RouteTopologyInternals::Failure(                                             \
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));                      \
		if (C != 1)                                                                                         \
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology, \
				TEXT("Required feature lacks unique coverage"), V.bHasId, V.Id);                            \
	}
	M5_REQUIRE_FEATURE(Chambers, Chamber)
	M5_REQUIRE_FEATURE(Connections, Connection)
	M5_REQUIRE_FEATURE(Segments, Segment)
	M5_REQUIRE_FEATURE(Entrances, Entrance)
	M5_REQUIRE_FEATURE(Portals, Portal)
	M5_REQUIRE_FEATURE(Junctions, Junction)
#undef M5_REQUIRE_FEATURE
	// Graph of route connections must itself be connected. BFS uses stable input
	// order and a bounded quadratic scan; it does not rely on hash iteration.
	TArray<uint8> Reached;
	Reached.Init(0, Plan.Routes.Num());
	TArray<int32> Queue;
	Queue.Reserve(Plan.Routes.Num());
	Reached[0] = 1;
	Queue.Add(0);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const auto& A = Plan.Routes[Queue[Head]];
		for (int32 I = 0; I < Plan.Routes.Num(); ++I)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			const auto& B = Plan.Routes[I];
			if (!Reached[I]
				&& ((FVoxelLandM5RouteTopologyInternals::SameNode(A.Start, B.Start)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(A.Legs[0].StartFeetCm, B.Legs[0].StartFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.Start, B.End)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs[0].StartFeetCm, B.Legs.Last().EndFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.End, B.Start)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs.Last().EndFeetCm, B.Legs[0].StartFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.End, B.End)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs.Last().EndFeetCm, B.Legs.Last().EndFeetCm))))
			{
				Reached[I] = 1;
				Queue.Add(I);
			}
		}
	}
	if (Queue.Num() != Plan.Routes.Num())
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Route graph is disconnected"));
	}
	// Independently require the unmarked edges to form the connected backbone.
	// The checks above establish a simple graph with E-N+1 marked loops, so a
	// connected non-loop subgraph incident to every node has exactly N-1 edges.
	TArray<uint8> IsLoop;
	IsLoop.Init(0, Plan.Routes.Num());
	int32 FirstTreeRoute = INDEX_NONE;
	int32 TreeRouteCount = 0;
	for (int32 I = 0; I < Plan.Routes.Num(); ++I)
	{
		for (const auto& Ref : Plan.Routes[I].CoveredFeatures)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			if (Ref.Kind != EVoxelLandM5RouteFeatureKind::Connection)
			{
				continue;
			}
			for (const auto& Connection : Plan.Connections)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteTopologyInternals::Failure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
				}
				if (Connection.bHasId && Connection.Id == Ref.Id)
				{
					IsLoop[I] = Connection.bLoop ? 1 : 0;
					if (!Connection.bLoop)
					{
						++TreeRouteCount;
						if (FirstTreeRoute == INDEX_NONE)
						{
							FirstTreeRoute = I;
						}
					}
				}
			}
		}
	}
	if (FirstTreeRoute == INDEX_NONE || int64(TreeRouteCount) != NodeCount - 1)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Non-loop edges do not form a spanning tree"));
	}
	Reached.Init(0, Plan.Routes.Num());
	Queue.Reset();
	Reached[FirstTreeRoute] = 1;
	Queue.Add(FirstTreeRoute);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const auto& A = Plan.Routes[Queue[Head]];
		for (int32 I = 0; I < Plan.Routes.Num(); ++I)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteTopologyInternals::Failure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
			}
			const auto& B = Plan.Routes[I];
			if (!IsLoop[I] && !Reached[I]
				&& ((FVoxelLandM5RouteTopologyInternals::SameNode(A.Start, B.Start)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(A.Legs[0].StartFeetCm, B.Legs[0].StartFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.Start, B.End)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs[0].StartFeetCm, B.Legs.Last().EndFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.End, B.Start)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs.Last().EndFeetCm, B.Legs[0].StartFeetCm))
					|| (FVoxelLandM5RouteTopologyInternals::SameNode(A.End, B.End)
						&& FVoxelLandM5RouteTopologyInternals::SamePoint(
							A.Legs.Last().EndFeetCm, B.Legs.Last().EndFeetCm))))
			{
				Reached[I] = 1;
				Queue.Add(I);
			}
		}
	}
	if (Queue.Num() != TreeRouteCount)
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Non-loop backbone is disconnected"));
	}
	const auto HasTreeEdge = [&Plan, &Region, &IsCancelled](
								 EVoxelLandM5EndpointKind Kind, bool bHasId, uint64 Id) -> int32 {
		if (!bHasId)
		{
			return 0;
		}
		FVoxelLandM5Endpoint Node;
		Node.bValid = true;
		Node.Kind = Kind;
		Node.Region = Region;
		Node.Id = Id;
		for (const auto& Connection : Plan.Connections)
		{
			if (IsCancelled())
			{
				return -1;
			}
			if (!Connection.bLoop
				&& (FVoxelLandM5RouteTopologyInternals::SameNode(Connection.Start, Node)
					|| FVoxelLandM5RouteTopologyInternals::SameNode(Connection.End, Node)))
			{
				return 1;
			}
		}
		return 0;
	};
#define M5_REQUIRE_TREE_EDGE(Array, Kind)                                                                   \
	for (const auto& V : Plan.Array)                                                                        \
	{                                                                                                       \
		const int32 Found = HasTreeEdge(EVoxelLandM5EndpointKind::Kind, V.bHasId, V.Id);                    \
		if (Found < 0)                                                                                      \
			return FVoxelLandM5RouteTopologyInternals::Failure(                                             \
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));                      \
		if (!Found)                                                                                         \
			return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::InconsistentTopology, \
				TEXT("Node is absent from non-loop backbone"), V.bHasId, V.Id);                             \
	}
	M5_REQUIRE_TREE_EDGE(Chambers, Chamber)
	M5_REQUIRE_TREE_EDGE(Entrances, Entrance)
	M5_REQUIRE_TREE_EDGE(Portals, Portal)
	M5_REQUIRE_TREE_EDGE(Junctions, Junction)
#undef M5_REQUIRE_TREE_EDGE
	if (IsCancelled())
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
	}
	return FVoxelLandM5Diagnostic();
}

FVoxelLandM5Diagnostic VoxelLand::M5::RouteTopology::Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	const FVoxelLandM5Diagnostic Result =
		ValidateStructure(Settings, WorldSeed, Region, Candidate, Budget, IsCancelled);
	if (Result.Code != EVoxelLandM5ErrorCode::None)
	{
		return Result;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5RouteTopologyInternals::Failure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Route validation cancelled"));
	}
	return FVoxelLandM5RouteTopologyInternals::Failure(EVoxelLandM5ErrorCode::Unproven,
		TEXT("Feature labels do not prove geometric traversal; final route validation is required"));
}
