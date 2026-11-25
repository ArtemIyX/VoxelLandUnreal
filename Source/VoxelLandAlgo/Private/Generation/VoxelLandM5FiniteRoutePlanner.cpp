#include "Generation/VoxelLandM5FiniteRoutePlanner.h"

#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5RouteBuilder.h"

class FVoxelLandM5FiniteRoutePlannerInternals final
{
public:
	static constexpr int64 MaxItems = 40000;
	static constexpr int64 CoordinateLimit = 1048576;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FiniteRoutePlanner");
		Result.Message = Message;
		return Result;
	}

	static bool SamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	static bool SameRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
	{
		return A.X == B.X && A.Y == B.Y;
	}

	static bool SameAnchor(const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B)
	{
		return A.bValid && B.bValid && A.Kind == B.Kind && A.Id == B.Id
			&& A.AnchorRole == B.AnchorRole && SameRegion(A.Region, B.Region);
	}

	static bool InRange(const FVoxelLandM5PointCm& P)
	{
		return P.X >= -CoordinateLimit && P.X <= CoordinateLimit
			&& P.Y >= -CoordinateLimit && P.Y <= CoordinateLimit
			&& P.Z >= -CoordinateLimit && P.Z <= CoordinateLimit;
	}

	static bool Inside(const FVoxelLandM5Bounds& B, const FVoxelLandM5PointCm& P)
	{
		return B.MinInclusive.X >= -CoordinateLimit && B.MinInclusive.Y >= -CoordinateLimit
			&& B.MinInclusive.Z >= -CoordinateLimit && B.MaxExclusive.X <= CoordinateLimit + 1
			&& B.MaxExclusive.Y <= CoordinateLimit + 1 && B.MaxExclusive.Z <= CoordinateLimit + 1
			&& B.MinInclusive.X < B.MaxExclusive.X && B.MinInclusive.Y < B.MaxExclusive.Y
			&& B.MinInclusive.Z < B.MaxExclusive.Z && P.X >= B.MinInclusive.X && P.X < B.MaxExclusive.X
			&& P.Y >= B.MinInclusive.Y && P.Y < B.MaxExclusive.Y && P.Z >= B.MinInclusive.Z
			&& P.Z < B.MaxExclusive.Z;
	}

	static bool Center(const FVoxelLandM5CavePlan& Plan, const FVoxelLandM5Endpoint& Endpoint,
		TFunctionRef<bool()> IsCancelled, bool& bCancelled, FVoxelLandM5PointCm& Out)
	{
		int32 Matches = 0;
		if (Endpoint.Kind == EVoxelLandM5EndpointKind::Chamber)
		{
			for (const FVoxelLandM5Chamber& Chamber : Plan.Chambers)
			{
				if (IsCancelled())
				{
					bCancelled = true;
					return false;
				}
				if (Chamber.bHasId && Chamber.Id == Endpoint.Id)
				{
					++Matches;
					Out = Chamber.CenterCm;
				}
			}
		}
		else if (Endpoint.Kind == EVoxelLandM5EndpointKind::Junction)
		{
			for (const FVoxelLandM5Junction& Junction : Plan.Junctions)
			{
				if (IsCancelled())
				{
					bCancelled = true;
					return false;
				}
				if (Junction.bHasId && Junction.bHasCenter && Junction.Id == Endpoint.Id)
				{
					++Matches;
					Out = Junction.CenterCm;
				}
			}
		}
		return Matches == 1 && InRange(Out);
	}

	static EVoxelLandM5AnchorRole Role(EVoxelLandM5EndpointKind Kind)
	{
		if (Kind == EVoxelLandM5EndpointKind::Chamber)
		{
			return EVoxelLandM5AnchorRole::ChamberFeet;
		}
		if (Kind == EVoxelLandM5EndpointKind::Junction)
		{
			return EVoxelLandM5AnchorRole::JunctionFeet;
		}
		if (Kind == EVoxelLandM5EndpointKind::Entrance)
		{
			return EVoxelLandM5AnchorRole::EntranceOutsideFeet;
		}
		return EVoxelLandM5AnchorRole::None;
	}

	static bool AddAnchor(TArray<FVoxelLandM5RouteAnchorChoice>& Anchors,
		const FVoxelLandM5Endpoint& Endpoint, const FVoxelLandM5PointCm& Feet,
		TFunctionRef<bool()> IsCancelled, bool& bCancelled)
	{
		for (const FVoxelLandM5RouteAnchorChoice& Existing : Anchors)
		{
			if (IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (SameAnchor(Existing.Endpoint, Endpoint))
			{
				return SamePoint(Existing.FeetCm, Feet);
			}
		}
		FVoxelLandM5RouteAnchorChoice Choice;
		Choice.Endpoint = Endpoint;
		Choice.FeetCm = Feet;
		Anchors.Add(MoveTemp(Choice));
		return true;
	}

	static FVoxelLandM5Diagnostic Feet(const FVoxelLandM5PointCm& CenterPoint,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandM5PointCm&,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>, FVoxelLandM5PointCm&)> ResolveFeet,
		TFunctionRef<bool()> IsCancelled, TArray<FVoxelLandM5PointCm>& Centers,
		TArray<FVoxelLandM5PointCm>& Results, FVoxelLandM5PointCm& Out)
	{
		for (int32 Index = 0; Index < Centers.Num(); ++Index)
		{
			if ((Index & 63) == 0 && IsCancelled())
			{
				return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet resolution cancelled"));
			}
			if (SamePoint(Centers[Index], CenterPoint))
			{
				Out = Results[Index];
				return {};
			}
		}
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet resolution cancelled"));
		}
		if (!InRange(CenterPoint))
		{
			return Error(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Feet center is outside the supported domain"));
		}
		FVoxelLandM5PointCm Proposed;
		const FVoxelLandM5Diagnostic Result = ResolveFeet(CenterPoint, Budget, IsCancelled, Proposed);
		if (Result.Code != EVoxelLandM5ErrorCode::None)
		{
			return Result;
		}
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet resolution cancelled"));
		}
		if (!InRange(Proposed) || Proposed.X != CenterPoint.X || Proposed.Y != CenterPoint.Y)
		{
			return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Feet proposal does not match centerline XY"));
		}
		Centers.Add(CenterPoint);
		Results.Add(Proposed);
		Out = Proposed;
		return {};
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::FiniteRoutePlanner::Build(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5RegionCoord& Region,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandM5PointCm&,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>, FVoxelLandM5PointCm&)> ResolveFeet,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out)
{
	using I = FVoxelLandM5FiniteRoutePlannerInternals;
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
	}
	const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
	if (Plan.GeometryVersion != 1)
		return I::Error(E::Unproven, TEXT("Profile route construction is not yet proven"));
	int64 MaxBytes = 0, MaxWork = 0;
	Budget->GetLimits(MaxBytes, MaxWork);
	const int64 ConnectionCount = Plan.Connections.Num();
	const int64 SegmentCount = Plan.Segments.Num();
	const int64 Count = ConnectionCount + SegmentCount + Plan.Chambers.Num() + Plan.Junctions.Num()
		+ Plan.Entrances.Num() + 1;
	if (!Settings.bEnabled || !Settings.StageId.IsValid()
		|| Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite || !Candidate->UsesBudget(*Budget) || !Candidate->MatchesSource(Settings, WorldSeed, Region)
		|| !I::SameRegion(Plan.Region, Region) || !Plan.Routes.IsEmpty() || !Plan.Portals.IsEmpty()
		|| ConnectionCount < 1 || SegmentCount < 1 || Settings.Limits.MaxRoutes < 1
		|| Settings.Limits.MaxRouteLegs < 1 || Settings.Limits.MaxSegments < 1
		|| ConnectionCount > Settings.Limits.MaxRoutes || SegmentCount > Settings.Limits.MaxSegments
		|| SegmentCount > Settings.Limits.MaxRouteLegs || Plan.Entrances.Num() != Settings.Topology.EntranceCount
		|| Count > I::MaxItems || MaxBytes > Settings.Limits.MaxBytes || MaxWork > Settings.Limits.MaxValidationWork)
	{
		return I::Error(E::InvalidInput, TEXT("Invalid finite route source or limits"));
	}
	int64 Work = 0, Bytes = 0;
	if (!TryMultiplyNonNegative(Count, Count, Work) || !TryMultiplyNonNegative(Work, 64, Work)
		|| !TryAdd(Work, 256, Work) || !TryMultiplyNonNegative(Count, 2048, Bytes)
		|| !TryAdd(Bytes, 32768, Bytes))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Finite route admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return I::Error(E::BudgetExceeded, TEXT("Finite route work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Finite route scratch exhausted"));
	}
	TArray<int32> Order;
	Order.Reserve(int32(ConnectionCount));
	for (int32 Index = 0; Index < Plan.Connections.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
		}
		const FVoxelLandM5Connection& C = Plan.Connections[Index];
		if (!C.bHasId || !C.Start.bValid || !C.End.bValid
			|| C.Start.AnchorRole != EVoxelLandM5AnchorRole::None || C.End.AnchorRole != EVoxelLandM5AnchorRole::None
			|| !I::SameRegion(C.Start.Region, Region) || !I::SameRegion(C.End.Region, Region))
		{
			return I::Error(E::InconsistentTopology, TEXT("Invalid finite connection"));
		}
		int32 At = Order.Num();
		Order.Add(Index);
		while (At > 0 && Plan.Connections[Order[At - 1]].Id > C.Id)
		{
			if ((At & 63) == 0 && IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
			}
			Order[At] = Order[At - 1];
			--At;
		}
		Order[At] = Index;
	}
	for (int32 Index = 1; Index < Order.Num(); ++Index)
	{
		if ((Index & 63) == 0 && IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
		}
		if (Plan.Connections[Order[Index - 1]].Id == Plan.Connections[Order[Index]].Id)
		{
			return I::Error(E::IdentityCollision, TEXT("Duplicate connection ID"));
		}
	}
	for (int32 Index = 0; Index < Plan.Segments.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
		}
		const FVoxelLandM5Segment& Segment = Plan.Segments[Index];
		if (!Segment.bHasId || !Segment.bHasConnectionId || I::SamePoint(Segment.StartCm, Segment.EndCm))
		{
			return I::Error(E::InconsistentTopology, TEXT("Invalid segment"));
		}
		int32 Owners = 0;
		for (const FVoxelLandM5Connection& C : Plan.Connections)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
			}
			Owners += C.Id == Segment.ConnectionId ? 1 : 0;
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			if ((Other & 63) == 0 && IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
			}
			if (Plan.Segments[Other].Id == Segment.Id)
			{
				return I::Error(E::IdentityCollision, TEXT("Duplicate segment ID"));
			}
		}
		if (Owners != 1)
		{
			return I::Error(E::InconsistentTopology, TEXT("Segment has no unique connection"));
		}
	}
	TArray<FVoxelLandM5RouteAnchorChoice> Anchors;
	TArray<FVoxelLandM5RouteConnectionChoice> Choices;
	TArray<FVoxelLandM5PointCm> CachedCenters, CachedFeet;
	TArray<uint8> Used;
	Anchors.Reserve(int32(2 * ConnectionCount + Plan.Entrances.Num()));
	Choices.Reserve(int32(ConnectionCount));
	CachedCenters.Reserve(int32(SegmentCount + ConnectionCount));
	CachedFeet.Reserve(int32(SegmentCount + ConnectionCount));
	Used.Init(0, int32(SegmentCount));
	for (int32 OrderedIndex : Order)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
		}
		const FVoxelLandM5Connection& C = Plan.Connections[OrderedIndex];
		FVoxelLandM5Endpoint Start = C.Start;
		FVoxelLandM5Endpoint End = C.End;
		if (Start.Kind == EVoxelLandM5EndpointKind::Entrance && End.Kind != EVoxelLandM5EndpointKind::Entrance)
		{
			Swap(Start, End);
		}
		Start.AnchorRole = I::Role(Start.Kind);
		End.AnchorRole = I::Role(End.Kind);
		if (Start.AnchorRole == EVoxelLandM5AnchorRole::None || End.AnchorRole == EVoxelLandM5AnchorRole::None
			|| Start.Kind == EVoxelLandM5EndpointKind::Entrance
			|| (Start.Kind == End.Kind && Start.Id == End.Id))
		{
			return I::Error(E::Unproven, TEXT("Unsupported finite connection endpoints"));
		}
		FVoxelLandM5PointCm Current, Target;
		bool bCancelled = false;
		if (!I::Center(Plan, Start, IsCancelled, bCancelled, Current)
			|| (End.Kind != EVoxelLandM5EndpointKind::Entrance
				&& !I::Center(Plan, End, IsCancelled, bCancelled, Target)))
		{
			return I::Error(bCancelled ? E::Cancelled : E::InconsistentTopology,
				TEXT("Connection center is missing, ambiguous or cancelled"));
		}
		int32 Expected = 0;
		for (const FVoxelLandM5Segment& Segment : Plan.Segments)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
			}
			Expected += Segment.ConnectionId == C.Id ? 1 : 0;
		}
		if (Expected == 0)
		{
			return I::Error(E::InconsistentTopology, TEXT("Connection has no segments"));
		}
		FVoxelLandM5RouteConnectionChoice Choice;
		Choice.bHasConnectionId = true;
		Choice.ConnectionId = C.Id;
		Choice.Start = Start;
		Choice.End = End;
		Choice.SegmentIds.Reserve(Expected);
		Choice.FeetWaypoints.Reserve(Expected + 1);
		TArray<FVoxelLandM5PointCm> ChainCenters;
		ChainCenters.Reserve(Expected + 1);
		ChainCenters.Add(Current);
		FVoxelLandM5PointCm FeetPoint;
		FVoxelLandM5Diagnostic Diagnostic = I::Feet(Current, Budget, ResolveFeet, IsCancelled,
			CachedCenters, CachedFeet, FeetPoint);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		Choice.FeetWaypoints.Add(FeetPoint);
		for (int32 Step = 0; Step < Expected; ++Step)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
			}
			int32 Match = INDEX_NONE;
			bool bReverse = false;
			for (int32 Index = 0; Index < Plan.Segments.Num(); ++Index)
			{
				if ((Index & 63) == 0 && IsCancelled())
				{
					return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
				}
				const FVoxelLandM5Segment& Segment = Plan.Segments[Index];
				if (Used[Index] || Segment.ConnectionId != C.Id)
				{
					continue;
				}
				const bool bStart = I::SamePoint(Current, Segment.StartCm);
				const bool bEnd = I::SamePoint(Current, Segment.EndCm);
				if (bStart || bEnd)
				{
					if (Match != INDEX_NONE)
					{
						return I::Error(E::InconsistentTopology, TEXT("Branched segment chain"));
					}
					Match = Index;
					bReverse = bEnd;
				}
			}
			if (Match == INDEX_NONE)
			{
				return I::Error(E::InconsistentTopology, TEXT("Disconnected segment chain"));
			}
			const FVoxelLandM5Segment& Segment = Plan.Segments[Match];
			if (Step == 0)
			{
				Choice.bReverseFirst = bReverse;
			}
			Used[Match] = 1;
			Current = bReverse ? Segment.StartCm : Segment.EndCm;
			for (const FVoxelLandM5PointCm& Prior : ChainCenters)
			{
				if (IsCancelled())
				{
					return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
				}
				if (I::SamePoint(Prior, Current))
				{
					return I::Error(E::InconsistentTopology, TEXT("Cyclic segment chain"));
				}
			}
			ChainCenters.Add(Current);
			Choice.SegmentIds.Add(Segment.Id);
			Diagnostic = I::Feet(Current, Budget, ResolveFeet, IsCancelled, CachedCenters, CachedFeet, FeetPoint);
			if (Diagnostic.Code != E::None)
			{
				return Diagnostic;
			}
			Choice.FeetWaypoints.Add(FeetPoint);
		}
		if (End.Kind != EVoxelLandM5EndpointKind::Entrance && !I::SamePoint(Current, Target))
		{
			return I::Error(E::InconsistentTopology, TEXT("Segment chain ends at the wrong feature"));
		}
		if (End.Kind == EVoxelLandM5EndpointKind::Entrance)
		{
			const FVoxelLandM5Entrance* Entry = nullptr;
			for (const FVoxelLandM5Entrance& Entrance : Plan.Entrances)
			{
				if (IsCancelled())
				{
					return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
				}
				if (Entrance.bHasId && Entrance.Id == End.Id)
				{
					if (Entry)
					{
						return I::Error(E::IdentityCollision, TEXT("Duplicate entrance ID"));
					}
					Entry = &Entrance;
				}
			}
			if (!Entry || !Entry->bHasConnectionId || Entry->ConnectionId != C.Id || Entry->bFlooded
				|| Expected < 2 || !I::Inside(Entry->OpeningBoundsCm, Current)
				|| !I::Inside(Entry->OpeningBoundsCm, ChainCenters[ChainCenters.Num() - 2]))
			{
				return I::Error(E::Unproven, TEXT("Entrance endpoints or opening are invalid"));
			}
			FVoxelLandM5Endpoint Inside = End;
			Inside.AnchorRole = EVoxelLandM5AnchorRole::EntranceInsideFeet;
			if (!I::AddAnchor(Anchors, Inside, Choice.FeetWaypoints[Choice.FeetWaypoints.Num() - 2],
					IsCancelled, bCancelled))
			{
				return I::Error(bCancelled ? E::Cancelled : E::InconsistentTopology,
					TEXT("Conflicting or cancelled entrance anchor"));
			}
		}
		if (!I::AddAnchor(Anchors, Start, Choice.FeetWaypoints[0], IsCancelled, bCancelled)
			|| !I::AddAnchor(Anchors, End, Choice.FeetWaypoints.Last(), IsCancelled, bCancelled))
		{
			return I::Error(bCancelled ? E::Cancelled : E::InconsistentTopology,
				TEXT("Conflicting or cancelled route anchor"));
		}
		Choices.Add(MoveTemp(Choice));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Finite route planning cancelled"));
	}
	return VoxelLand::M5::RouteBuilder::Build(Settings, WorldSeed, Region, Candidate, Anchors, Choices,
		Budget, IsCancelled, Out);
}
