#include "Generation/VoxelLandM5RouteBuilder.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FeetChain.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Generation/VoxelLandM5SegmentCoverage.h"

class FVoxelLandM5RouteBuilderInternals final
{
public:
	static FVoxelLandM5Diagnostic M5RouteBuilderFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static bool M5RouteBuilderSameRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B);
	static bool M5RouteBuilderSamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B);
	static bool M5RouteBuilderSameNode(const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B);
	static bool M5RouteBuilderSameAnchor(const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B);
	static bool M5RouteBuilderSamePortalKey(const FVoxelLandM5PortalKey& A, const FVoxelLandM5PortalKey& B);
	static EVoxelLandM5RouteFeatureKind M5RouteBuilderFeatureKind(EVoxelLandM5EndpointKind Kind);
	static FVoxelLandM5RouteFeatureRef M5RouteBuilderRef(
		EVoxelLandM5RouteFeatureKind Kind, const FVoxelLandM5RegionCoord& Region, uint64 Id);
	static bool M5RouteBuilderSetAnchor(FVoxelLandM5CavePlan& Plan, const FVoxelLandM5RouteAnchorChoice& Choice,
		const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed, TFunctionRef<bool()> IsCancelled, bool& bCancelled);
};

FVoxelLandM5Diagnostic FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Error;
	Error.Code = Code;
	Error.Stage = TEXT("M5.RouteBuilder");
	Error.Message = Message;
	return Error;
}
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameRegion(
	const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X == B.X && A.Y == B.Y;
}
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(
	const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(
	const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B)
{
	return A.bValid && B.bValid && A.Kind == B.Kind && A.Id == B.Id && M5RouteBuilderSameRegion(A.Region, B.Region);
}
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameAnchor(
	const FVoxelLandM5Endpoint& A, const FVoxelLandM5Endpoint& B)
{
	return M5RouteBuilderSameNode(A, B) && A.AnchorRole == B.AnchorRole;
}
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePortalKey(
	const FVoxelLandM5PortalKey& A, const FVoxelLandM5PortalKey& B)
{
	return A.WorldSeed == B.WorldSeed && A.StageId == B.StageId && A.Salt == B.Salt && A.Axis == B.Axis
		&& M5RouteBuilderSameRegion(A.RegionA, B.RegionA) && M5RouteBuilderSameRegion(A.RegionB, B.RegionB);
}
EVoxelLandM5RouteFeatureKind FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFeatureKind(EVoxelLandM5EndpointKind Kind)
{
	switch (Kind)
	{
		case EVoxelLandM5EndpointKind::Chamber:
			return EVoxelLandM5RouteFeatureKind::Chamber;
		case EVoxelLandM5EndpointKind::Entrance:
			return EVoxelLandM5RouteFeatureKind::Entrance;
		case EVoxelLandM5EndpointKind::Portal:
			return EVoxelLandM5RouteFeatureKind::Portal;
		default:
			return EVoxelLandM5RouteFeatureKind::Junction;
	}
}
FVoxelLandM5RouteFeatureRef FVoxelLandM5RouteBuilderInternals::M5RouteBuilderRef(
	EVoxelLandM5RouteFeatureKind Kind, const FVoxelLandM5RegionCoord& Region, uint64 Id)
{
	FVoxelLandM5RouteFeatureRef Result;
	Result.bValid = true;
	Result.Kind = Kind;
	Result.Region = Region;
	Result.Id = Id;
	return Result;
}
// Require exactly one destination and never overwrite an existing different
// anchor. Portal sides are selected by the canonical key, not array order.
bool FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSetAnchor(FVoxelLandM5CavePlan& Plan,
	const FVoxelLandM5RouteAnchorChoice& Choice, const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TFunctionRef<bool()> IsCancelled, bool& bCancelled)
{
	const auto& E = Choice.Endpoint;
	if (!E.bValid || (E.Kind != EVoxelLandM5EndpointKind::Portal && !M5RouteBuilderSameRegion(E.Region, Plan.Region)))
	{
		return false;
	}
	int32 Matches = 0;
	auto Set = [&Choice, &Matches](bool& Present, FVoxelLandM5PointCm& Point) {
		++Matches;
		if (Present && !M5RouteBuilderSamePoint(Point, Choice.FeetCm))
		{
			Matches = -1000000;
			return;
		}
		Present = true;
		Point = Choice.FeetCm;
	};
	if (E.Kind == EVoxelLandM5EndpointKind::Chamber && E.AnchorRole == EVoxelLandM5AnchorRole::ChamberFeet)
	{
		for (auto& V : Plan.Chambers)
		{
			if (IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (V.bHasId && V.Id == E.Id)
			{
				Set(V.bHasFeetAnchor, V.FeetAnchorCm);
			}
		}
	}
	else if (E.Kind == EVoxelLandM5EndpointKind::Junction && E.AnchorRole == EVoxelLandM5AnchorRole::JunctionFeet)
	{
		for (auto& V : Plan.Junctions)
		{
			if (IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (V.bHasId && V.Id == E.Id)
			{
				Set(V.bHasFeetAnchor, V.FeetAnchorCm);
			}
		}
	}
	else if (E.Kind == EVoxelLandM5EndpointKind::Entrance)
	{
		for (auto& V : Plan.Entrances)
		{
			if (IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (!V.bHasId || V.Id != E.Id)
			{
				continue;
			}
			if (E.AnchorRole == EVoxelLandM5AnchorRole::EntranceInsideFeet)
			{
				Set(V.bHasInsideFeetAnchor, V.InsideFeetAnchorCm);
			}
			else if (E.AnchorRole == EVoxelLandM5AnchorRole::EntranceOutsideFeet)
			{
				Set(V.bHasOutsideFeetAnchor, V.OutsideFeetAnchorCm);
			}
		}
	}
	else if (E.Kind == EVoxelLandM5EndpointKind::Portal)
	{
		if (!Choice.bHasPortalKey)
		{
			return false;
		}
		const auto& K = Choice.PortalKey;
		if (K.WorldSeed != WorldSeed || K.StageId != Settings.StageId || K.Salt != Settings.Salt
			|| (!M5RouteBuilderSameRegion(Plan.Region, K.RegionA) && !M5RouteBuilderSameRegion(Plan.Region, K.RegionB))
			|| K.RegionA.X > K.RegionB.X || (K.RegionA.X == K.RegionB.X && K.RegionA.Y >= K.RegionB.Y))
		{
			return false;
		}
		const bool bAxisMatches = (K.Axis == EVoxelLandM5PortalAxis::X && K.RegionA.Y == K.RegionB.Y)
							   || (K.Axis == EVoxelLandM5PortalAxis::Y && K.RegionA.X == K.RegionB.X);
		TArray<uint8> Digest;
		uint64 ExpectedId = 0;
		if (!bAxisMatches
			|| !VoxelLand::M5::Seed::TryPortalDigest(WorldSeed, Settings.StageId, static_cast<uint64>(Settings.Salt),
				K.RegionA.X, K.RegionA.Y, K.RegionB.X, K.RegionB.Y, Digest)
			|| !VoxelLand::M5::Seed::TryDigestWord(Digest, ExpectedId) || ExpectedId != E.Id)
		{
			return false;
		}
		for (auto& V : Plan.Portals)
		{
			if (IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (!V.bHasId || V.Id != E.Id || !M5RouteBuilderSamePortalKey(V.Key, K))
			{
				continue;
			}
			if (E.AnchorRole == EVoxelLandM5AnchorRole::PortalRegionAFeet
				&& M5RouteBuilderSameRegion(E.Region, V.Key.RegionA))
			{
				Set(V.bHasRegionAFeetAnchor, V.RegionAFeetAnchorCm);
			}
			else if (E.AnchorRole == EVoxelLandM5AnchorRole::PortalRegionBFeet
					 && M5RouteBuilderSameRegion(E.Region, V.Key.RegionB))
			{
				Set(V.bHasRegionBFeetAnchor, V.RegionBFeetAnchorCm);
			}
		}
	}
	return Matches == 1;
}

FVoxelLandM5Diagnostic VoxelLand::M5::RouteBuilder::Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TConstArrayView<FVoxelLandM5RouteAnchorChoice> Anchors, TConstArrayView<FVoxelLandM5RouteConnectionChoice> Choices,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out)
{
	if (IsCancelled())
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
	}
	const auto& Input = Candidate->GetPlan();
	const auto& Limits = Settings.Limits;
	int64 BudgetBytes = 0, BudgetWork = 0;
	Budget->GetLimits(BudgetBytes, BudgetWork);
	if (!Candidate->UsesBudget(Budget.Get()) || !Candidate->MatchesSource(Settings, WorldSeed, Region)
		|| BudgetBytes > Limits.MaxBytes || BudgetWork > Limits.MaxValidationWork || !Input.Routes.IsEmpty()
		|| !FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameRegion(Input.Region, Region) || Limits.MaxRoutes < 1
		|| Limits.MaxRoutes > 16384 || Limits.MaxRouteLegs < 1 || Limits.MaxRouteLegs > 65536
		|| Limits.MaxRouteFeatureRefs < 1 || Limits.MaxRouteFeatureRefs > 131072 || Limits.MaxSegments < 1
		|| Limits.MaxSegments > 8192 || Choices.IsEmpty() || Choices.Num() != Input.Connections.Num()
		|| Choices.Num() > Limits.MaxRoutes || Input.Segments.Num() > Limits.MaxSegments
		|| Anchors.Num() > 2 * Limits.MaxRouteFeatureRefs)
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid source, choices or plan limits"));
	}
	// Fixed public caps bound all products below int64 limits. Charge before
	// scanning caller arrays; input storage remains caller-owned.
	const int64 Count = int64(Anchors.Num()) + Choices.Num() + Input.Segments.Num() + Input.Chambers.Num()
					  + Input.Portals.Num() + Input.Entrances.Num() + Input.Junctions.Num() + 1;
	// Portal anchor validation hashes one canonical packet per choice; charge
	// the same conservative SHA allowance used by topology validation.
	if (Count > 300000 || !Budget->TryChargeWork(256 + 32 * Count * Count + 1024 * int64(Anchors.Num())))
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route construction work limit exceeded"));
	}
	int64 TotalLegs = 0;
	for (const auto& Choice : Choices)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		if (!Choice.bHasConnectionId || Choice.SegmentIds.IsEmpty() || Choice.SegmentIds.Num() > Limits.MaxSegments)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Missing connection or segment chain"));
		}
		if (!Choice.FeetWaypoints.IsEmpty() && Choice.FeetWaypoints.Num() != Choice.SegmentIds.Num() + 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Waypoint count does not match segment chain"));
		}
		TotalLegs += Choice.SegmentIds.Num();
		if (TotalLegs > Limits.MaxRouteLegs || TotalLegs > Input.Segments.Num())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route leg limit exceeded"));
		}
	}
	const int64 MaxRefs = TotalLegs + 3 * int64(Choices.Num());
	if (MaxRefs > Limits.MaxRouteFeatureRefs)
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route feature limit exceeded"));
	}
	const int64 Bytes =
		32768
		+ 8
			  * (Count * 512 + TotalLegs * (int64(sizeof(FVoxelLandM5RouteLeg)) + int64(sizeof(FVoxelLandM5Segment)))
				  + MaxRefs * int64(sizeof(FVoxelLandM5RouteFeatureRef)));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route memory limit exceeded"));
	}
	FVoxelLandM5CavePlan Scratch = Input;
	// A compact ID may resolve to only one full key within a plan. Reject even
	// if the caller happens to provide one of two colliding keys.
	for (int32 I = 0; I < Scratch.Portals.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			if (Scratch.Portals[I].bHasId && Scratch.Portals[J].bHasId
				&& Scratch.Portals[I].Id == Scratch.Portals[J].Id)
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate portal ID"));
			}
		}
	}
	for (int32 I = 0; I < Anchors.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameAnchor(Anchors[I].Endpoint, Anchors[J].Endpoint))
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate anchor choice"));
			}
		}
		bool bCancelled = false;
		if (!FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSetAnchor(
				Scratch, Anchors[I], Settings, WorldSeed, IsCancelled, bCancelled))
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				bCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::InvalidInput,
				TEXT("Invalid, conflicting or cancelled anchor"));
		}
	}
	// Sort by connection ID using bounded insertion sort: ordering is independent
	// of authored choice order and does not depend on unstable container order.
	TArray<int32> Order;
	Order.Reserve(Choices.Num());
	for (int32 I = 0; I < Choices.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		int32 At = Order.Num();
		Order.Add(I);
		while (At > 0 && Choices[Order[At - 1]].ConnectionId > Choices[I].ConnectionId)
		{
			if ((At & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			Order[At] = Order[At - 1];
			--At;
		}
		Order[At] = I;
	}
	for (int32 I = 1; I < Order.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		if (Choices[Order[I - 1]].ConnectionId == Choices[Order[I]].ConnectionId)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate connection choice"));
		}
	}
	for (const int32 Index : Order)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
		}
		const auto& Choice = Choices[Index];
		if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(Choice.Start, Choice.End))
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Self-connected route is unsupported"));
		}
		int32 Connections = 0;
		for (const auto& C : Input.Connections)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			if (C.bHasId && C.Id == Choice.ConnectionId && C.Start.AnchorRole == EVoxelLandM5AnchorRole::None
				&& C.End.AnchorRole == EVoxelLandM5AnchorRole::None
				&& ((FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(C.Start, Choice.Start)
						&& FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(C.End, Choice.End))
					|| (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(C.Start, Choice.End)
						&& FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameNode(C.End, Choice.Start))))
			{
				++Connections;
			}
		}
		if (Connections != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Connection endpoints do not match"));
		}
		const FVoxelLandM5RouteAnchorChoice* Start = nullptr;
		const FVoxelLandM5RouteAnchorChoice* End = nullptr;
		for (const auto& A : Anchors)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameAnchor(A.Endpoint, Choice.Start))
			{
				Start = &A;
			}
			if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameAnchor(A.Endpoint, Choice.End))
			{
				End = &A;
			}
		}
		if (!Start || !End)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Explicit endpoint anchors are required"));
		}
		TArray<FVoxelLandM5Segment> Segments;
		Segments.Reserve(Choice.SegmentIds.Num());
		for (const uint64 Id : Choice.SegmentIds)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
			}
			const FVoxelLandM5Segment* Found = nullptr;
			for (const auto& S : Input.Segments)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				if (S.bHasId && S.Id == Id)
				{
					if (Found)
					{
						return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
							EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate segment ID"));
					}
					Found = &S;
				}
			}
			if (!Found || !Found->bHasConnectionId || Found->ConnectionId != Choice.ConnectionId)
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Segment does not belong to connection"));
			}
			Segments.Add(*Found);
		}
		FVoxelLandM5Route Route;
		Route.bHasId = true;
		Route.Id = Choice.ConnectionId;
		Route.Start = Choice.Start;
		Route.End = Choice.End;
		Route.CoveredFeatures.Add(FVoxelLandM5RouteBuilderInternals::M5RouteBuilderRef(
			EVoxelLandM5RouteFeatureKind::Connection, Region, Choice.ConnectionId));
		if (Choice.FeetWaypoints.IsEmpty())
		{
			TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe> Chain;
			auto Error = FVoxelLandM5FeetChain::Build(Segments, Choice.ConnectionId, Choice.bReverseFirst, Settings.Capsule,
				Limits.MaxSegments, Budget, IsCancelled, Chain);
			if (Error.Code != EVoxelLandM5ErrorCode::None)
			{
				return Error;
			}
			if (!Chain.IsValid() || Chain->GetLegs().IsEmpty()
				|| !FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(
					Chain->GetLegs()[0].Feet.StartFeetCm, Start->FeetCm)
				|| !FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(
					Chain->GetLegs().Last().Feet.EndFeetCm, End->FeetCm))
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Feet chain does not match explicit anchors"));
			}
			for (const auto& Leg : Chain->GetLegs())
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				FVoxelLandM5RouteLeg R;
				R.StartFeetCm = Leg.Feet.StartFeetCm;
				R.EndFeetCm = Leg.Feet.EndFeetCm;
				R.Feature = FVoxelLandM5RouteBuilderInternals::M5RouteBuilderRef(
					EVoxelLandM5RouteFeatureKind::Segment, Region, Leg.SegmentId);
				Route.Legs.Add(R);
				Route.CoveredFeatures.Add(R.Feature);
			}
		}
		else
		{
			if (!FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Choice.FeetWaypoints[0], Start->FeetCm)
				|| !FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Choice.FeetWaypoints.Last(), End->FeetCm))
			{
				return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Waypoints do not match explicit anchors"));
			}
			FVoxelLandM5PointCm PreviousCenter;
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				const auto& Segment = Segments[Index];
				for (int32 Previous = 0; Previous < Index; ++Previous)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
					}
					if (Segments[Previous].Id == Segment.Id)
					{
						return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
							EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate segment ID in route"));
					}
				}
				bool bReverse = Choice.bReverseFirst;
				if (Index > 0)
				{
					if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(PreviousCenter, Segment.StartCm))
					{
						bReverse = false;
					}
					else if (FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(PreviousCenter, Segment.EndCm))
					{
						bReverse = true;
					}
					else
					{
						return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
							EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Segment chain is discontinuous"));
					}
				}
				const auto& CenterStart = bReverse ? Segment.EndCm : Segment.StartCm;
				const auto& CenterEnd = bReverse ? Segment.StartCm : Segment.EndCm;
				const auto& WaypointStart = Choice.FeetWaypoints[Index];
				const auto& WaypointEnd = Choice.FeetWaypoints[Index + 1];
				if (WaypointStart.X != CenterStart.X || WaypointStart.Y != CenterStart.Y
					|| WaypointEnd.X != CenterEnd.X || WaypointEnd.Y != CenterEnd.Y)
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Waypoint direction does not match segment chain"));
				}
				FVoxelLandM5RouteLeg R;
				R.StartFeetCm = WaypointStart;
				R.EndFeetCm = WaypointEnd;
				R.Feature = FVoxelLandM5RouteBuilderInternals::M5RouteBuilderRef(
					EVoxelLandM5RouteFeatureKind::Segment, Region, Segment.Id);
				const auto Error = VoxelLand::M5::SegmentCoverage::Check(
					Segment, R, Region, Settings.Capsule, Budget, IsCancelled);
				if (Error.Code != EVoxelLandM5ErrorCode::None)
				{
					return Error;
				}
				Route.Legs.Add(R);
				Route.CoveredFeatures.Add(R.Feature);
				PreviousCenter = CenterEnd;
			}
		}
		for (const FVoxelLandM5Endpoint* E : {&Choice.Start, &Choice.End})
		{
			const auto Wanted = FVoxelLandM5RouteBuilderInternals::M5RouteBuilderRef(
				FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFeatureKind(E->Kind), Region, E->Id);
			bool bOwned = false;
			for (const auto& Previous : Scratch.Routes)
			{
				for (const auto& Existing : Previous.CoveredFeatures)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
					}
					bOwned |=
						Existing.Kind == Wanted.Kind && Existing.Id == Wanted.Id
						&& FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameRegion(Existing.Region, Wanted.Region);
				}
			}
			if (!bOwned)
			{
				Route.CoveredFeatures.Add(Wanted);
			}
		}
		Scratch.Routes.Add(MoveTemp(Route));
	}
	// Reject missing, duplicate or mislabelled features before handing the
	// result to any later structural or geometric validator.
	for (const auto& Segment : Input.Segments)
	{
		int32 Uses = 0;
		for (const auto& Route : Scratch.Routes)
		{
			for (const auto& Leg : Route.Legs)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				Uses += Leg.Feature.Kind == EVoxelLandM5RouteFeatureKind::Segment && Leg.Feature.Id == Segment.Id;
			}
		}
		if (!Segment.bHasId || Uses != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Segment does not have exactly one route leg"));
		}
	}
	bool bCoverageCancelled = false;
	auto Covered = [&Scratch, &Region, &IsCancelled, &bCoverageCancelled](
					   EVoxelLandM5RouteFeatureKind Kind, bool bHasId, uint64 Id) {
		if (!bHasId)
		{
			return 0;
		}
		int32 CountMatches = 0;
		for (const auto& Route : Scratch.Routes)
		{
			for (const auto& F : Route.CoveredFeatures)
			{
				if (IsCancelled())
				{
					bCoverageCancelled = true;
					return -1;
				}
				CountMatches += F.bValid && F.Kind == Kind && F.Id == Id
							 && FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameRegion(F.Region, Region);
			}
		}
		return CountMatches;
	};
	for (const auto& C : Scratch.Chambers)
	{
		if (!C.bHasFeetAnchor || Covered(EVoxelLandM5RouteFeatureKind::Chamber, C.bHasId, C.Id) != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				bCoverageCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
				TEXT("Chamber anchor or route is missing"));
		}
	}
	for (const auto& P : Scratch.Portals)
	{
		if (!P.bHasRegionAFeetAnchor || !P.bHasRegionBFeetAnchor
			|| Covered(EVoxelLandM5RouteFeatureKind::Portal, P.bHasId, P.Id) != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				bCoverageCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
				TEXT("Portal route is missing"));
		}
	}
	for (const auto& J : Scratch.Junctions)
	{
		if (!J.bHasFeetAnchor || Covered(EVoxelLandM5RouteFeatureKind::Junction, J.bHasId, J.Id) != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				bCoverageCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
				TEXT("Junction anchor or route is missing"));
		}
	}
	for (const auto& E : Scratch.Entrances)
	{
		if (!E.bHasInsideFeetAnchor || !E.bHasOutsideFeetAnchor
			|| Covered(EVoxelLandM5RouteFeatureKind::Entrance, E.bHasId, E.Id) != 1)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				bCoverageCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
				TEXT("Entrance anchors or route are missing"));
		}
		bool bBoth = false;
		for (const auto& Route : Scratch.Routes)
		{
			bool bOwned = false, bInside = false, bOutside = false;
			for (const auto& F : Route.CoveredFeatures)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				bOwned |= F.bValid && F.Kind == EVoxelLandM5RouteFeatureKind::Entrance && F.Id == E.Id
					   && FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSameRegion(F.Region, Region);
			}
			for (const auto& Leg : Route.Legs)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
				}
				bInside |=
					FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Leg.StartFeetCm, E.InsideFeetAnchorCm)
					|| FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Leg.EndFeetCm, E.InsideFeetAnchorCm);
				bOutside |=
					FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Leg.StartFeetCm, E.OutsideFeetAnchorCm)
					|| FVoxelLandM5RouteBuilderInternals::M5RouteBuilderSamePoint(Leg.EndFeetCm, E.OutsideFeetAnchorCm);
			}
			bBoth |= bOwned && bInside && bOutside;
		}
		if (!bBoth)
		{
			return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Entrance route does not join both anchors"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5RouteBuilderInternals::M5RouteBuilderFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Route construction cancelled"));
	}
	TSharedPtr<FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Complete = MakeShareable(
		new FVoxelLandM5CandidateResult(Budget, Settings, WorldSeed, Region, MoveTemp(Scratch), MoveTemp(Lease)));
	Complete->DirectSource = Candidate;
	Out = MoveTemp(Complete);
	return FVoxelLandM5Diagnostic();
}
