#include "Generation/Caves/Planning/VoxelLandCaveEntrancePlanner.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Planning/VoxelLandCaveRegionalSeed.h"
#include "Generation/Caves/Planning/VoxelLandCaveSeed.h"

class FVoxelLandM5EntrancePlannerInternals final
{
public:
	static constexpr int64 M5EntranceCoordinateLimit = 1048576;
	static constexpr int32 M5EntranceHardCount = 256;
	static constexpr uint32 M5EntranceMaxDraws = 64;

	using FSurfaceQuery = TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		int64, int64, int32, int64&, int64&, int64&, int32&, bool&, bool&)>;

	struct FPlacementContext
	{
		const FVoxelLandM5CaveSettings& Settings;
		const FVoxelLandM5CavePlan& Candidate;
		const TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget;
		FSurfaceQuery QuerySurface;
		TFunctionRef<bool()> IsCancelled;
		int64 Seed;
		int64 Radius;
		int64 Offset;
		int64 QueryRadius;
		int64 MinX;
		int64 MaxX;
		int64 MinY;
		int64 MaxY;
	};

	static FVoxelLandM5Diagnostic AdmitEntrancePlanning(const FVoxelLandM5CavePlan& Candidate,
		const FVoxelLandM5CaveSettings& S, const TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& Lease);
	static FVoxelLandM5Diagnostic PlaceEntrance(
		const FPlacementContext& Context, int32 Index, FVoxelLandM5CavePlan& Scratch);

private:
	struct FEntrancePlacement
	{
		FVoxelLandM5PointCm Surface;
		FVoxelLandM5PointCm Approach;
		FVoxelLandM5PointCm Outside;
		bool bFlooded;
	};

	static const FVoxelLandM5Chamber* FindApproachChamber(
		TConstArrayView<FVoxelLandM5Chamber> Chambers, const FVoxelLandM5PointCm& Approach, int32 MaxSlopeDegrees);
	static FVoxelLandM5Diagnostic AppendEntranceGeometry(const FPlacementContext& Context, int32 Index,
		const FEntrancePlacement& Placement, const FVoxelLandM5Chamber& Chamber, FVoxelLandM5CavePlan& Scratch);

public:
	static FVoxelLandM5Diagnostic M5EntranceError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.EntrancePlanner");
		Result.Message = Message;
		return Result;
	}

	static bool M5EntranceBoundsValid(const FVoxelLandM5Bounds& B)
	{
		const auto Valid = [](int64 Min, int64 Max) {
			return Min >= -M5EntranceCoordinateLimit && Min <= M5EntranceCoordinateLimit && Max > Min
				&& Max <= M5EntranceCoordinateLimit + 1;
		};
		return Valid(B.MinInclusive.X, B.MaxExclusive.X) && Valid(B.MinInclusive.Y, B.MaxExclusive.Y)
			&& Valid(B.MinInclusive.Z, B.MaxExclusive.Z);
	}

	static bool M5EntranceContains(const FVoxelLandM5Bounds& B, const FVoxelLandM5PointCm& P)
	{
		return P.X >= B.MinInclusive.X && P.X < B.MaxExclusive.X && P.Y >= B.MinInclusive.Y && P.Y < B.MaxExclusive.Y
			&& P.Z >= B.MinInclusive.Z && P.Z < B.MaxExclusive.Z;
	}

	// For 0 <= degrees < 90, degrees/90 is no larger than tan(degrees).
	// max(|dx|,|dy|) is no larger than horizontal Euclidean distance. Thus
	// satisfying this integer bound conservatively limits the true angle.
	static bool M5EntranceSlopeAllowed(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, int32 Degrees)
	{
		const int64 DX = FMath::Abs(B.X - A.X);
		const int64 DY = FMath::Abs(B.Y - A.Y);
		const int64 DZ = FMath::Abs(B.Z - A.Z);
		return DZ * 90 <= FMath::Max(DX, DY) * int64(Degrees);
	}

	static bool M5EntranceRandom(const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5RegionCoord& Region,
		uint64 EntranceId, uint32 Attempt, uint32 FirstDraw, int64 Min, int64 Max, TFunctionRef<bool()> IsCancelled,
		int64& Out)
	{
		uint32 Draws = 0;
		return VoxelLand::M5::Seed::TryUniformSigned(
			Min, Max, FirstDraw, M5EntranceMaxDraws,
			[&S, Seed, &Region, EntranceId, Attempt](uint32 Draw, uint64& Word) {
				TArray<uint8> Digest;
				const bool bOk = S.PlanningMode == EVoxelLandM5PlanningMode::Regional
								   ? VoxelLand::M5::RegionalSeed::TryStreamDigest(3, Seed, S.StageId, uint64(S.Salt),
										 Region.X, Region.Y, EntranceId, Attempt, Draw, Digest)
								   : VoxelLand::M5::Seed::TryStreamDigest(
										 3, Seed, S.StageId, uint64(S.Salt), EntranceId, Attempt, Draw, Digest);
				return bOk && VoxelLand::M5::Seed::TryDigestWord(Digest, Word);
			},
			IsCancelled, Out, Draws);
	}

	template <typename T> static bool M5EntranceIdsValid(const TArray<T>& Items, TFunctionRef<bool()> IsCancelled)
	{
		for (int32 I = 0; I < Items.Num(); ++I)
		{
			if (IsCancelled() || !Items[I].bHasId)
			{
				return false;
			}
			for (int32 J = 0; J < I; ++J)
			{
				if ((J % 256 == 0 && IsCancelled()) || Items[I].Id == Items[J].Id)
				{
					return false;
				}
			}
		}
		return true;
	}

	template <typename T>
	static bool M5EntranceNextId(const TArray<T>& Items, TFunctionRef<bool()> IsCancelled, uint64& OutId)
	{
		// At most Items.Num() IDs can occupy this bounded prefix.
		for (uint64 Id = 0; Id < uint64(Items.Num()); ++Id)
		{
			if (IsCancelled())
			{
				return false;
			}
			bool bFound = false;
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (Index % 256 == 0 && IsCancelled())
				{
					return false;
				}
				if (Items[Index].Id == Id)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				OutId = Id;
				return true;
			}
		}
		if (IsCancelled())
		{
			return false;
		}
		OutId = uint64(Items.Num());
		return true;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5EntrancePlannerInternals::AdmitEntrancePlanning(
	const FVoxelLandM5CavePlan& Candidate, const FVoxelLandM5CaveSettings& S,
	const TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& Lease)
{
	using namespace VoxelLand::M5::CheckedMath;
	const FVoxelLandM5TopologySettings& T = S.Topology;
	const FVoxelLandM5ResourceLimits& L = S.Limits;
	const int64 ExistingEdges = Candidate.Connections.Num();
	// Precharge a conservative upper bound for comparisons, cancellation points,
	// bounded SHA packet operations and plan copying. Each of two samplers can
	// consume 64 draws, each with a SHA and cancellation check, plus a final
	// check. The trusted surface provider charges its own work separately.
	const int64 ChamberCount = Candidate.Chambers.Num();
	const int64 SegmentCount = Candidate.Segments.Num();
	const int64 Work =
		1024 + 8 * (ChamberCount * ChamberCount + ExistingEdges * ExistingEdges + SegmentCount * SegmentCount)
		+ int64(T.EntranceCount)
			  * (8 * (ExistingEdges + T.EntranceCount) * (ExistingEdges + T.EntranceCount)
				  + 16 * (SegmentCount + 2 * int64(T.EntranceCount)) * (SegmentCount + 2 * int64(T.EntranceCount))
				  + int64(L.MaxAttempts)
						* (2 * (2 * int64(FVoxelLandM5EntrancePlannerInternals::M5EntranceMaxDraws) + 1)
							+ 16 * ChamberCount + 32));
	if (Work > L.MaxValidationWork)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance work exceeds budget"));
	}
	int64 Bytes = 0;
	int64 Part = 0;
	const auto Charge = [&Bytes, &Part](int64 Count, int64 Size) {
		return TryMultiplyNonNegative(Count, Size, Part) && TryAdd(Bytes, Part, Bytes);
	};
	if (!Charge(Candidate.Chambers.Num(), sizeof(FVoxelLandM5Chamber))
		|| !Charge(Candidate.Portals.Num(), sizeof(FVoxelLandM5Portal))
		|| !Charge(ExistingEdges + T.EntranceCount, sizeof(FVoxelLandM5Connection))
		|| !Charge(int64(Candidate.Segments.Num()) + 2 * int64(T.EntranceCount), sizeof(FVoxelLandM5Segment))
		|| !Charge(T.EntranceCount, sizeof(FVoxelLandM5Entrance)) || !TryMultiplyNonNegative(Bytes, 4, Bytes)
		|| !TryAdd(Bytes, 16384, Bytes))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Entrance admission overflow"));
	}
	if (Bytes > L.MaxBytes)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance scratch exceeds memory budget"));
	}
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance memory admission failed"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance work admission failed"));
	}
	return {};
}

const FVoxelLandM5Chamber* FVoxelLandM5EntrancePlannerInternals::FindApproachChamber(
	TConstArrayView<FVoxelLandM5Chamber> Chambers, const FVoxelLandM5PointCm& Approach, int32 MaxSlopeDegrees)
{
	const FVoxelLandM5Chamber* Nearest = nullptr;
	uint64 Best = MAX_uint64;
	for (const FVoxelLandM5Chamber& Chamber : Chambers)
	{
		// Keep the whole oblique approach centerline at or below this
		// plane; only the vertical segment crosses the queried footprint.
		if (Chamber.CenterCm.Z > Approach.Z
			|| !FVoxelLandM5EntrancePlannerInternals::M5EntranceSlopeAllowed(
				Chamber.CenterCm, Approach, MaxSlopeDegrees))
		{
			continue;
		}
		const int64 DX = Chamber.CenterCm.X - Approach.X, DY = Chamber.CenterCm.Y - Approach.Y,
					DZ = Chamber.CenterCm.Z - Approach.Z;
		const uint64 D = uint64(DX * DX + DY * DY + DZ * DZ);
		if (D != 0 && (D < Best || (D == Best && (!Nearest || Chamber.Id < Nearest->Id))))
		{
			Nearest = &Chamber;
			Best = D;
		}
	}
	return Nearest;
}

FVoxelLandM5Diagnostic FVoxelLandM5EntrancePlannerInternals::AppendEntranceGeometry(const FPlacementContext& Context,
	int32 Index, const FEntrancePlacement& Placement, const FVoxelLandM5Chamber& Chamber, FVoxelLandM5CavePlan& Scratch)
{
	const FVoxelLandM5PointCm& Surface = Placement.Surface;
	const FVoxelLandM5PointCm& Approach = Placement.Approach;
	const FVoxelLandM5PointCm& Outside = Placement.Outside;
	const bool bFlooded = Placement.bFlooded;
	const FVoxelLandM5CaveSettings& S = Context.Settings;
	const TFunctionRef<bool()> IsCancelled = Context.IsCancelled;
	const int64 QueryRadius = Context.QueryRadius;
	const int64 Radius = Context.Radius;
	FVoxelLandM5Entrance Entrance;
	Entrance.bHasId = Entrance.bHasConnectionId = true;
	Entrance.Id = uint64(Index);
	if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceNextId(
			Scratch.Connections, IsCancelled, Entrance.ConnectionId))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance ID search cancelled"));
	}
	Entrance.bFlooded = bFlooded && S.Water.Policy == EVoxelLandM5WaterPolicy::AllowAndMark;
	Entrance.OpeningBoundsCm.MinInclusive.X = Surface.X - QueryRadius;
	Entrance.OpeningBoundsCm.MinInclusive.Y = Surface.Y - QueryRadius;
	Entrance.OpeningBoundsCm.MinInclusive.Z = Surface.Z - Radius;
	Entrance.OpeningBoundsCm.MaxExclusive.X = Surface.X + QueryRadius + 1;
	Entrance.OpeningBoundsCm.MaxExclusive.Y = Surface.Y + QueryRadius + 1;
	Entrance.OpeningBoundsCm.MaxExclusive.Z = Outside.Z + Radius + 1;
	FVoxelLandM5Connection Connection;
	Connection.bHasId = true;
	Connection.Id = Entrance.ConnectionId;
	Connection.Start.bValid = Connection.End.bValid = true;
	Connection.Start.Kind = EVoxelLandM5EndpointKind::Chamber;
	Connection.Start.Region = Connection.End.Region = Context.Candidate.Region;
	Connection.Start.Id = Chamber.Id;
	Connection.End.Kind = EVoxelLandM5EndpointKind::Entrance;
	Connection.End.Id = Entrance.Id;
	FVoxelLandM5Segment Segment;
	Segment.bHasId = Segment.bHasConnectionId = true;
	if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceNextId(Scratch.Segments, IsCancelled, Segment.Id))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance ID search cancelled"));
	}
	Segment.ConnectionId = Connection.Id;
	Segment.StartCm = Chamber.CenterCm;
	Segment.EndCm = Approach;
	Segment.RadiusCm = int32(Radius);
	Scratch.Segments.Add(Segment);
	FVoxelLandM5Segment Vertical = Segment;
	if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceNextId(Scratch.Segments, IsCancelled, Vertical.Id))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance ID search cancelled"));
	}
	Vertical.StartCm = Approach;
	Vertical.EndCm = Outside;
	Scratch.Segments.Add(Vertical);
	Scratch.Entrances.Add(Entrance);
	Scratch.Connections.Add(Connection);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5EntrancePlannerInternals::PlaceEntrance(
	const FPlacementContext& Context, int32 Index, FVoxelLandM5CavePlan& Scratch)
{
	const FVoxelLandM5CaveSettings& S = Context.Settings;
	const FVoxelLandM5CavePlan& Candidate = Context.Candidate;
	const FVoxelLandM5Bounds& B = Candidate.Bounds;
	const FVoxelLandM5ResourceLimits& L = S.Limits;
	const FVoxelLandM5CapsuleProfile& Capsule = S.Capsule;
	const TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget = Context.Budget;
	const FSurfaceQuery QuerySurface = Context.QuerySurface;
	const TFunctionRef<bool()> IsCancelled = Context.IsCancelled;
	const int64 Seed = Context.Seed, Radius = Context.Radius, Offset = Context.Offset,
				QueryRadius = Context.QueryRadius;
	const int64 MinX = Context.MinX, MaxX = Context.MaxX, MinY = Context.MinY, MaxY = Context.MaxY;
	bool bPlaced = false;
	bool bSlopeUnproven = false;
	for (int32 Attempt = 0; Attempt < L.MaxAttempts; ++Attempt)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance planning cancelled"));
		}
		FVoxelLandM5PointCm Surface;
		if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceRandom(
				S, Seed, Candidate.Region, uint64(Index), uint32(Attempt), 0, MinX, MaxX, IsCancelled, Surface.X)
			|| !FVoxelLandM5EntrancePlannerInternals::M5EntranceRandom(
				S, Seed, Candidate.Region, uint64(Index), uint32(Attempt), 64, MinY, MaxY, IsCancelled, Surface.Y))
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				IsCancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Blocked,
				TEXT("Entrance random stream exhausted"));
		}
		int32 Slope = 0;
		int64 MinSurfaceZ = 0, MaxSurfaceZ = 0;
		bool bProtected = false, bFlooded = false;
		const FVoxelLandM5Diagnostic SurfaceError = QuerySurface(Budget, Surface.X, Surface.Y, int32(QueryRadius),
			Surface.Z, MinSurfaceZ, MaxSurfaceZ, Slope, bProtected, bFlooded);
		if (SurfaceError.Code != EVoxelLandM5ErrorCode::None)
		{
			return SurfaceError;
		}
		if (bFlooded && S.Water.Policy == EVoxelLandM5WaterPolicy::DryWorld)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Water observation contradicts the dry-world declaration"));
		}
		if (Slope < 0 || Slope > 90 || Surface.Z < -FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit
			|| Surface.Z > FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit
			|| MinSurfaceZ < -FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit
			|| MaxSurfaceZ > FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit || MinSurfaceZ > Surface.Z
			|| Surface.Z > MaxSurfaceZ)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Final-surface query returned invalid data"));
		}
		if (MinSurfaceZ != MaxSurfaceZ)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Varying surface footprint is not supported"));
		}
		if (bProtected || Slope > Capsule.MaxSlopeDegrees
			|| (bFlooded && S.Water.Policy == EVoxelLandM5WaterPolicy::RejectFlooded))
		{
			continue;
		}
		FVoxelLandM5PointCm Outside = Surface;
		Outside.X += Offset;
		Outside.Z += int64(Capsule.StandingHeightCm) + Capsule.ClearanceMarginCm;
		if (Surface.Z - Radius < B.MinInclusive.Z || Outside.Z + Radius >= B.MaxExclusive.Z)
		{
			continue;
		}
		FVoxelLandM5PointCm Approach = Surface;
		Approach.Z -= Radius;
		const FVoxelLandM5Chamber* Nearest = FindApproachChamber(Scratch.Chambers, Approach, Capsule.MaxSlopeDegrees);
		if (!Nearest)
		{
			bSlopeUnproven = true;
			continue;
		}
		if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceSlopeAllowed(Approach, Outside, Capsule.MaxSlopeDegrees))
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Cannot prove entrance centerline slope"));
		}
		const FVoxelLandM5Diagnostic GeometryError =
			AppendEntranceGeometry(Context, Index, {Surface, Approach, Outside, bFlooded}, *Nearest, Scratch);
		if (GeometryError.Code != EVoxelLandM5ErrorCode::None)
		{
			return GeometryError;
		}
		bPlaced = true;
		break;
	}
	if (!bPlaced)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			bSlopeUnproven ? EVoxelLandM5ErrorCode::Unproven : EVoxelLandM5ErrorCode::Blocked,
			TEXT("No provable eligible entrance within the attempt budget"));
	}
	return {};
}

FVoxelLandM5Diagnostic VoxelLand::M5::EntrancePlanner::Build(const FVoxelLandM5CaveSettings& S, int64 Seed,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CandidateResult,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
		int64&, int64&, int64&, int32&, bool&, bool&)>
		QuerySurface,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out)
{
	if (IsCancelled())
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance planning cancelled"));
	}
	if (!CandidateResult->UsesBudget(*Budget)
		|| !CandidateResult->MatchesSource(S, Seed, CandidateResult->GetPlan().Region))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Candidate budget or source descriptor mismatch"));
	}
	const FVoxelLandM5CavePlan& Candidate = CandidateResult->GetPlan();
	if (Candidate.GeometryVersion != 1)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Entrance geometry is not available for this profile version"));
	}
	// Routes own nested arrays and junctions add another array. This stage does
	// not yet account for either, so reject before making an uncharged copy.
	if (!Candidate.Routes.IsEmpty() || !Candidate.Junctions.IsEmpty())
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(EVoxelLandM5ErrorCode::Unproven,
			TEXT("Entrance planning cannot yet copy route or junction records under the shared budget"));
	}
	const auto& T = S.Topology;
	const auto& L = S.Limits;
	const auto& Capsule = S.Capsule;
	const FVoxelLandM5Bounds& B = Candidate.Bounds;
	int64 BudgetMaxBytes = 0, BudgetMaxWork = 0;
	Budget->GetLimits(BudgetMaxBytes, BudgetMaxWork);
	if (BudgetMaxBytes > L.MaxBytes || BudgetMaxWork > L.MaxValidationWork)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Shared budget limits exceed authored per-plan limits"));
	}
	if (!S.bEnabled || !S.StageId.IsValid() || !FVoxelLandM5EntrancePlannerInternals::M5EntranceBoundsValid(B)
		|| (S.PlanningMode != EVoxelLandM5PlanningMode::Finite && S.PlanningMode != EVoxelLandM5PlanningMode::Regional)
		|| Candidate.Chambers.IsEmpty() || !Candidate.Entrances.IsEmpty() || T.EntranceCount < 0
		|| T.EntranceCount > FVoxelLandM5EntrancePlannerInternals::M5EntranceHardCount || L.MaxAttempts < 1
		|| L.MaxAttempts > 64 || L.MaxEdges < 1 || L.MaxSegments < 1 || L.MaxBytes < 1 || L.MaxValidationWork < 1
		|| Capsule.RadiusCm < 1 || Capsule.StandingHeightCm < 2 * int64(Capsule.RadiusCm)
		|| Capsule.ClearanceMarginCm < 0 || Capsule.MaxSlopeDegrees < 0 || Capsule.MaxSlopeDegrees > 90
		|| T.MaxTunnelRadiusCm < 1
		|| T.MaxTunnelRadiusCm > FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit
		|| 2 * int64(T.MaxTunnelRadiusCm) < int64(Capsule.StandingHeightCm) + 2 * int64(Capsule.ClearanceMarginCm)
		|| (S.Water.Policy != EVoxelLandM5WaterPolicy::Ignore
			&& S.Water.Policy != EVoxelLandM5WaterPolicy::RejectFlooded
			&& S.Water.Policy != EVoxelLandM5WaterPolicy::AllowAndMark
			&& S.Water.Policy != EVoxelLandM5WaterPolicy::DryWorld))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid entrance settings or candidate plan"));
	}
	const int64 ExistingNodes = int64(Candidate.Chambers.Num()) + Candidate.Portals.Num();
	const int64 ExistingEdges = Candidate.Connections.Num();
	if (ExistingNodes > 260 || ExistingEdges > 33670 || Candidate.Segments.Num() > 33670
		|| Candidate.Segments.Num() > L.MaxSegments || ExistingEdges + T.EntranceCount > L.MaxEdges
		|| int64(Candidate.Segments.Num()) + 2 * int64(T.EntranceCount) > L.MaxSegments)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance count exceeds plan budget"));
	}
	using namespace VoxelLand::M5::CheckedMath;
	if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceBoundsValid(S.Bounds)
		|| B.MinInclusive.X < S.Bounds.MinInclusive.X || B.MinInclusive.Y < S.Bounds.MinInclusive.Y
		|| B.MinInclusive.Z < S.Bounds.MinInclusive.Z || B.MaxExclusive.X > S.Bounds.MaxExclusive.X
		|| B.MaxExclusive.Y > S.Bounds.MaxExclusive.Y || B.MaxExclusive.Z > S.Bounds.MaxExclusive.Z)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Candidate lies outside configured domain"));
	}
	if (S.PlanningMode == EVoxelLandM5PlanningMode::Finite)
	{
		if (B.MinInclusive.X != S.Bounds.MinInclusive.X || B.MinInclusive.Y != S.Bounds.MinInclusive.Y
			|| B.MinInclusive.Z != S.Bounds.MinInclusive.Z || B.MaxExclusive.X != S.Bounds.MaxExclusive.X
			|| B.MaxExclusive.Y != S.Bounds.MaxExclusive.Y || B.MaxExclusive.Z != S.Bounds.MaxExclusive.Z)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Finite candidate bounds differ from settings"));
		}
	}
	else
	{
		int64 X = 0, Y = 0, EndX = 0, EndY = 0;
		if (S.RegionSizeCm <= 0 || !TryMultiply(Candidate.Region.X, S.RegionSizeCm, X)
			|| !TryMultiply(Candidate.Region.Y, S.RegionSizeCm, Y) || !TryAdd(X, S.RegionOriginXCm, X)
			|| !TryAdd(Y, S.RegionOriginYCm, Y) || !TryAdd(X, S.RegionSizeCm, EndX) || !TryAdd(Y, S.RegionSizeCm, EndY)
			|| X != B.MinInclusive.X || Y != B.MinInclusive.Y || EndX != B.MaxExclusive.X || EndY != B.MaxExclusive.Y
			|| B.MinInclusive.Z != S.Bounds.MinInclusive.Z || B.MaxExclusive.Z != S.Bounds.MaxExclusive.Z)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Candidate region does not match its bounds"));
		}
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	const FVoxelLandM5Diagnostic AdmissionError =
		FVoxelLandM5EntrancePlannerInternals::AdmitEntrancePlanning(Candidate, S, Budget, Lease);
	if (AdmissionError.Code != EVoxelLandM5ErrorCode::None)
	{
		return AdmissionError;
	}
	if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceIdsValid(Candidate.Chambers, IsCancelled)
		|| !FVoxelLandM5EntrancePlannerInternals::M5EntranceIdsValid(Candidate.Connections, IsCancelled)
		|| !FVoxelLandM5EntrancePlannerInternals::M5EntranceIdsValid(Candidate.Segments, IsCancelled))
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			IsCancelled() ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::InconsistentTopology,
			TEXT("Candidate validation cancelled or has missing or duplicate IDs"));
	}
	for (const FVoxelLandM5Chamber& Chamber : Candidate.Chambers)
	{
		if (!FVoxelLandM5EntrancePlannerInternals::M5EntranceContains(B, Chamber.CenterCm)
			|| Chamber.RadiusCm < T.MaxTunnelRadiusCm)
		{
			return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid chamber geometry"));
		}
	}
	const int64 Radius = T.MaxTunnelRadiusCm;
	if (T.EntranceCount > 0 && Capsule.MaxSlopeDegrees == 0)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("A horizontal-only profile cannot ascend to the surface"));
	}
	// Chosen conservative ratio Degrees/90 avoids platform trigonometry. The
	// rising ramp extends from surface - radius to the outside capsule center.
	const int64 Rise = Radius + int64(Capsule.StandingHeightCm) + Capsule.ClearanceMarginCm;
	const int64 Offset = T.EntranceCount > 0 ? (Rise * 90 + Capsule.MaxSlopeDegrees - 1) / Capsule.MaxSlopeDegrees : 0;
	const int64 QueryRadius = Radius + Offset;
	if (QueryRadius > FVoxelLandM5EntrancePlannerInternals::M5EntranceCoordinateLimit)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Entrance ramp footprint exceeds coordinate limits"));
	}
	const int64 MinX = B.MinInclusive.X + QueryRadius, MaxX = B.MaxExclusive.X - 1 - QueryRadius;
	const int64 MinY = B.MinInclusive.Y + QueryRadius, MaxY = B.MaxExclusive.Y - 1 - QueryRadius;
	if (MinX > MaxX || MinY > MaxY)
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("No opening footprint fits in the domain"));
	}
	FVoxelLandM5CavePlan Scratch = Candidate;
	const FVoxelLandM5EntrancePlannerInternals::FPlacementContext Context{
		S, Candidate, Budget, QuerySurface, IsCancelled, Seed, Radius, Offset, QueryRadius, MinX, MaxX, MinY, MaxY};
	for (int32 Index = 0; Index < T.EntranceCount; ++Index)
	{
		const FVoxelLandM5Diagnostic Error =
			FVoxelLandM5EntrancePlannerInternals::PlaceEntrance(Context, Index, Scratch);
		if (Error.Code != EVoxelLandM5ErrorCode::None)
		{
			return Error;
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance planning cancelled"));
	}
	TSharedPtr<FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Result = MakeShareable(
		new FVoxelLandM5CandidateResult(Budget, S, Seed, Candidate.Region, MoveTemp(Scratch), MoveTemp(Lease)));
	const FVoxelLandM5Diagnostic IdentityError = Result->BindCanonicalPlan(IsCancelled);
	if (IdentityError.Code != EVoxelLandM5ErrorCode::None)
	{
		return IdentityError;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5EntrancePlannerInternals::M5EntranceError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance planning cancelled"));
	}
	Out = MoveTemp(Result);
	return {};
}
