#include "Generation/VoxelLandM5EntranceMask.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5RouteTopology.h"
#include "Generation/VoxelLandM5Topology.h"
#include "Data/Enums/VoxelLandM5PortalAxis.h"

class FVoxelLandM5EntranceMaskInternals final
{
public:
	static constexpr int64 M5EntranceMaskLimitCm = 1048576;
	static FVoxelLandM5Diagnostic M5EntranceMaskError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static int64 M5EntranceMaskAxis(const FVoxelLandM5PointCm& Point, int32 Axis);
	static int64 M5EntranceMaskAxis(const FIntVector& Point, int32 Axis);
	static bool M5EntranceMaskBoundsValid(const FVoxelLandM5Bounds& Bounds);
	static bool M5EntranceMaskContains(const FVoxelLandM5Bounds& Outer, const FVoxelLandM5Bounds& Inner);
	static bool M5EntranceMaskOverlaps(const FVoxelLandM5Bounds& A, const FVoxelLandM5Bounds& B);
	static bool M5EntranceMaskRegionBounds(
		const FVoxelLandM5CaveSettings& Settings, const FVoxelLandM5RegionCoord& Region, FVoxelLandM5Bounds& Out);
	static bool M5EntranceMaskSameBounds(const FVoxelLandM5Bounds& A, const FVoxelLandM5Bounds& B);
	static bool M5EntranceMaskPortalFits(const FVoxelLandM5CaveSettings& Settings, const FVoxelLandM5Portal& Portal);
};

FVoxelLandM5Diagnostic FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.EntranceMask");
	Result.Message = Message;
	return Result;
}

int64 FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(const FVoxelLandM5PointCm& Point, int32 Axis)
{
	return Axis == 0 ? Point.X : Axis == 1 ? Point.Y : Point.Z;
}

int64 FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(const FIntVector& Point, int32 Axis)
{
	return Axis == 0 ? Point.X : Axis == 1 ? Point.Y : Point.Z;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskBoundsValid(const FVoxelLandM5Bounds& Bounds)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Min = M5EntranceMaskAxis(Bounds.MinInclusive, Axis);
		const int64 Max = M5EntranceMaskAxis(Bounds.MaxExclusive, Axis);
		if (Min < -M5EntranceMaskLimitCm || Max > M5EntranceMaskLimitCm + 1 || Min >= Max)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskContains(
	const FVoxelLandM5Bounds& Outer, const FVoxelLandM5Bounds& Inner)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (M5EntranceMaskAxis(Inner.MinInclusive, Axis) < M5EntranceMaskAxis(Outer.MinInclusive, Axis)
			|| M5EntranceMaskAxis(Inner.MaxExclusive, Axis) > M5EntranceMaskAxis(Outer.MaxExclusive, Axis))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskOverlaps(const FVoxelLandM5Bounds& A, const FVoxelLandM5Bounds& B)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (M5EntranceMaskAxis(A.MaxExclusive, Axis) <= M5EntranceMaskAxis(B.MinInclusive, Axis)
			|| M5EntranceMaskAxis(B.MaxExclusive, Axis) <= M5EntranceMaskAxis(A.MinInclusive, Axis))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskRegionBounds(
	const FVoxelLandM5CaveSettings& Settings, const FVoxelLandM5RegionCoord& Region, FVoxelLandM5Bounds& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (Settings.RegionSizeCm <= 0 || Settings.RegionSizeCm > 2 * M5EntranceMaskLimitCm
		|| Settings.RegionOriginXCm < -M5EntranceMaskLimitCm || Settings.RegionOriginXCm > M5EntranceMaskLimitCm
		|| Settings.RegionOriginYCm < -M5EntranceMaskLimitCm || Settings.RegionOriginYCm > M5EntranceMaskLimitCm)
	{
		return false;
	}
	int64 MinX = 0, MaxX = 0, MinY = 0, MaxY = 0, X = 0, Y = 0, XEnd = 0, YEnd = 0;
	if (!TrySubtract(Settings.Bounds.MinInclusive.X, Settings.RegionOriginXCm, MinX)
		|| !TrySubtract(Settings.Bounds.MaxExclusive.X, Settings.RegionOriginXCm, MaxX)
		|| !TrySubtract(Settings.Bounds.MinInclusive.Y, Settings.RegionOriginYCm, MinY)
		|| !TrySubtract(Settings.Bounds.MaxExclusive.Y, Settings.RegionOriginYCm, MaxY) || MinX % Settings.RegionSizeCm
		|| MaxX % Settings.RegionSizeCm || MinY % Settings.RegionSizeCm || MaxY % Settings.RegionSizeCm
		|| !TryMultiply(Region.X, Settings.RegionSizeCm, X) || !TryAdd(X, Settings.RegionOriginXCm, X)
		|| !TryMultiply(Region.Y, Settings.RegionSizeCm, Y) || !TryAdd(Y, Settings.RegionOriginYCm, Y)
		|| !TryAdd(X, Settings.RegionSizeCm, XEnd) || !TryAdd(Y, Settings.RegionSizeCm, YEnd)
		|| X < Settings.Bounds.MinInclusive.X || XEnd > Settings.Bounds.MaxExclusive.X
		|| Y < Settings.Bounds.MinInclusive.Y || YEnd > Settings.Bounds.MaxExclusive.Y)
	{
		return false;
	}
	Out = Settings.Bounds;
	Out.MinInclusive.X = X;
	Out.MinInclusive.Y = Y;
	Out.MaxExclusive.X = XEnd;
	Out.MaxExclusive.Y = YEnd;
	return true;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskSameBounds(
	const FVoxelLandM5Bounds& A, const FVoxelLandM5Bounds& B)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (M5EntranceMaskAxis(A.MinInclusive, Axis) != M5EntranceMaskAxis(B.MinInclusive, Axis)
			|| M5EntranceMaskAxis(A.MaxExclusive, Axis) != M5EntranceMaskAxis(B.MaxExclusive, Axis))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5EntranceMaskInternals::M5EntranceMaskPortalFits(
	const FVoxelLandM5CaveSettings& Settings, const FVoxelLandM5Portal& Portal)
{
	using namespace VoxelLand::M5::CheckedMath;
	FVoxelLandM5Bounds A, B;
	if (!M5EntranceMaskRegionBounds(Settings, Portal.Key.RegionA, A)
		|| !M5EntranceMaskRegionBounds(Settings, Portal.Key.RegionB, B) || Portal.RadiusCm <= 0
		|| Portal.RadiusCm < Settings.Topology.MinTunnelRadiusCm
		|| Portal.RadiusCm > Settings.Topology.MaxTunnelRadiusCm)
	{
		return false;
	}
	int64 Min[3], Max[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!TrySubtract(M5EntranceMaskAxis(Portal.CenterCm, Axis), Portal.RadiusCm, Min[Axis])
			|| !TryAdd(M5EntranceMaskAxis(Portal.CenterCm, Axis), Portal.RadiusCm, Max[Axis])
			|| Min[Axis] < M5EntranceMaskAxis(Settings.Bounds.MinInclusive, Axis)
			|| Max[Axis] >= M5EntranceMaskAxis(Settings.Bounds.MaxExclusive, Axis))
		{
			return false;
		}
	}
	if (Portal.Key.Axis == EVoxelLandM5PortalAxis::X)
	{
		return A.MaxExclusive.X == B.MinInclusive.X && Portal.CenterCm.X == A.MaxExclusive.X
			&& Min[1] >= A.MinInclusive.Y && Max[1] < A.MaxExclusive.Y;
	}
	if (Portal.Key.Axis == EVoxelLandM5PortalAxis::Y)
	{
		return A.MaxExclusive.Y == B.MinInclusive.Y && Portal.CenterCm.Y == A.MaxExclusive.Y
			&& Min[0] >= A.MinInclusive.X && Max[0] < A.MaxExclusive.X;
	}
	return false;
}

FVoxelLandM5EntranceMask::FVoxelLandM5EntranceMask(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5EntranceMask::Build(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection,
	const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask)
{
	return BuildForJob(Size, OriginCm, SpacingCm, Start, Count, Protection, Settings, WorldSeed, Candidates,
		Budget, Budget, IsCancelled, OutMask);
}

FVoxelLandM5Diagnostic FVoxelLandM5EntranceMask::BuildForJob(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection,
	const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask)
{
	return BuildImpl(Size, OriginCm, SpacingCm, Start, Count, Protection, Settings, WorldSeed, Candidates,
		SourceBudget, Budget, IsCancelled, OutMask, false);
}

FVoxelLandM5Diagnostic FVoxelLandM5EntranceMask::BuildGatedForJob(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection,
	const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask)
{
	return BuildImpl(Size, OriginCm, SpacingCm, Start, Count, Protection, Settings, WorldSeed, Candidates,
		SourceBudget, Budget, IsCancelled, OutMask, true);
}

FVoxelLandM5Diagnostic FVoxelLandM5EntranceMask::BuildImpl(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection,
	const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask, bool bGated)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
	}
	if (!Settings.bEnabled || !Settings.StageId.IsValid()
		|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskBoundsValid(Settings.Bounds) || Candidates.IsEmpty()
		|| Candidates.Num() > 64 || SpacingCm <= 0 || SpacingCm > 65536 || Settings.Limits.MaxBytes < 1
		|| Settings.Limits.MaxValidationWork < 1
		|| (Settings.PlanningMode == EVoxelLandM5PlanningMode::Finite && Candidates.Num() != 1)
		|| (Settings.PlanningMode != EVoxelLandM5PlanningMode::Finite
			&& Settings.PlanningMode != EVoxelLandM5PlanningMode::Regional))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid entrance mask inputs"));
	}
	int64 BudgetBytes = 0, BudgetWork = 0;
	Budget->GetLimits(BudgetBytes, BudgetWork);
	if (BudgetBytes > Settings.Limits.MaxBytes || BudgetWork > Settings.Limits.MaxValidationWork)
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Shared budget exceeds plan limits"));
	}
	int64 OutputXY = 0, Outputs = 0, Influence = 0;
	if (Count.X <= 0 || Count.Y <= 0 || Count.Z <= 0 || !TryMultiplyNonNegative(Count.X, Count.Y, OutputXY)
		|| !TryMultiplyNonNegative(OutputXY, Count.Z, Outputs) || Outputs > MAX_int32 || Protection.Num() != Outputs
		|| !TryMultiply(SpacingCm, 2, Influence))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid entrance mask grid size"));
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Size, Axis),
					S = FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Start, Axis);
		const int64 C = FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Count, Axis),
					O = FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(OriginCm, Axis);
		int64 GridDelta = 0, GridEnd = 0, FirstDelta = 0, LastDelta = 0, First = 0, Last = 0;
		if (N < 5 || S < 2 || C < 1 || S + C > N - 2 || O < -FVoxelLandM5EntranceMaskInternals::M5EntranceMaskLimitCm
			|| O > FVoxelLandM5EntranceMaskInternals::M5EntranceMaskLimitCm || !TryMultiply(N - 1, SpacingCm, GridDelta)
			|| !TryAdd(O, GridDelta, GridEnd) || GridEnd > FVoxelLandM5EntranceMaskInternals::M5EntranceMaskLimitCm
			|| !TryMultiply(S, SpacingCm, FirstDelta) || !TryMultiply(S + C - 1, SpacingCm, LastDelta)
			|| !TryAdd(O, FirstDelta, First) || !TryAdd(O, LastDelta, Last) || !TrySubtract(First, Influence, First)
			|| !TryAdd(Last, Influence, Last)
			|| (!bGated && (First < FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Settings.Bounds.MinInclusive, Axis)
				|| Last >= FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Settings.Bounds.MaxExclusive, Axis))))
		{
			return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Missing entrance influence halo"));
		}
	}
	// Charge provenance, cancellation and the bounded quadratic region scan
	// before inspecting any candidate. These units are separate from the later
	// opening/sample work and from the structural validators' own charges.
	int64 Preflight = 0, PreflightLinear = 0;
	if (!TryMultiplyNonNegative(Candidates.Num(), Candidates.Num(), Preflight)
		|| !TryMultiplyNonNegative(Preflight, 16, Preflight)
		|| !TryMultiplyNonNegative(Candidates.Num(), 64, PreflightLinear)
		|| !TryAdd(Preflight, PreflightLinear, Preflight) || !TryAdd(Preflight, 64, Preflight))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Entrance preflight work overflow"));
	}
	if (!Budget->TryChargeWork(Preflight))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance preflight admission exhausted"));
	}
	int64 TotalOpenings = 0;
	for (int32 I = 0; I < Candidates.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
		}
		if (!Candidates[I].IsValid() || !Candidates[I]->UsesBudget(SourceBudget.Get()))
		{
			return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Missing plan or mismatched budget"));
		}
		const FVoxelLandM5CavePlan& Plan = Candidates[I]->GetPlan();
		if (!Candidates[I]->MatchesSource(Settings, WorldSeed, Plan.Region)
			|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskBoundsValid(Plan.Bounds)
			|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskContains(Settings.Bounds, Plan.Bounds)
			|| !TryAdd(TotalOpenings, Plan.Entrances.Num(), TotalOpenings) || TotalOpenings > 4096)
		{
			return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid plan provenance or bounds"));
		}
		FVoxelLandM5Bounds ExpectedBounds;
		if ((Settings.PlanningMode == EVoxelLandM5PlanningMode::Regional
				&& (!FVoxelLandM5EntranceMaskInternals::M5EntranceMaskRegionBounds(
						Settings, Plan.Region, ExpectedBounds)
					|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskSameBounds(ExpectedBounds, Plan.Bounds)))
			|| (Settings.PlanningMode == EVoxelLandM5PlanningMode::Finite
				&& !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskSameBounds(Settings.Bounds, Plan.Bounds)))
		{
			return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Plan does not match its planning domain"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			const FVoxelLandM5RegionCoord& Other = Candidates[J]->GetPlan().Region;
			if (Other.X == Plan.Region.X && Other.Y == Plan.Region.Y)
			{
				return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate plan region"));
			}
		}
	}
	int64 PairWork = 0, SampleWork = 0, Work = 0, Bytes = 0, PlansBytes = 0, PortalWork = 0;
	if (!TryMultiplyNonNegative(TotalOpenings, TotalOpenings, PairWork)
		|| !TryMultiplyNonNegative(PairWork, 16, PairWork)
		|| !TryMultiplyNonNegative(TotalOpenings + 1, Outputs, SampleWork)
		|| !TryMultiplyNonNegative(SampleWork, 16, SampleWork) || !TryAdd(PairWork, SampleWork, Work)
		|| !TryMultiplyNonNegative(Candidates.Num(), 256, PortalWork) || !TryAdd(Work, PortalWork, Work)
		|| !TryAdd(Work, 64, Work) || !TryMultiplyNonNegative(TotalOpenings, 2 * sizeof(FVoxelLandM5Bounds), Bytes)
		|| !TryMultiplyNonNegative(Candidates.Num(),
			2 * sizeof(TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>), PlansBytes)
		|| !TryAdd(Bytes, PlansBytes, Bytes) || !TryAdd(Bytes, Outputs, Bytes) || !TryAdd(Bytes, 4096, Bytes))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Entrance mask admission overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Entrance mask admission exhausted"));
	}
	TArray<FVoxelLandM5Bounds> Openings;
	Openings.Reserve(static_cast<int32>(TotalOpenings));
	for (const auto& Candidate : Candidates)
	{
		const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
		const FVoxelLandM5Diagnostic Diagnostic =
			Plan.Routes.IsEmpty() && Plan.Junctions.IsEmpty()
				? VoxelLand::M5::Topology::ValidateForJob(
					  Settings, WorldSeed, Plan.Region, Candidate.ToSharedRef(), SourceBudget, Budget, IsCancelled)
				: VoxelLand::M5::RouteTopology::ValidateStructureForJob(
					  Settings, WorldSeed, Plan.Region, Candidate.ToSharedRef(), SourceBudget, Budget, IsCancelled);
		if (Diagnostic.Code != EVoxelLandM5ErrorCode::None)
		{
			return Diagnostic;
		}
		for (const FVoxelLandM5Portal& Portal : Plan.Portals)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
			}
			if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Regional
				|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskPortalFits(Settings, Portal))
			{
				return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
					EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Portal does not fit its shared region face"));
			}
		}
		for (const FVoxelLandM5Entrance& Entrance : Plan.Entrances)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
			}
			if (!Entrance.bHasId || !Entrance.bHasConnectionId
				|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskBoundsValid(Entrance.OpeningBoundsCm)
				|| !FVoxelLandM5EntranceMaskInternals::M5EntranceMaskContains(Plan.Bounds, Entrance.OpeningBoundsCm))
			{
				return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid entrance opening bounds"));
			}
			for (const FVoxelLandM5Bounds& Existing : Openings)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
				}
				if (FVoxelLandM5EntranceMaskInternals::M5EntranceMaskOverlaps(Existing, Entrance.OpeningBoundsCm))
				{
					return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
						EVoxelLandM5ErrorCode::Unproven, TEXT("Overlapping openings are ambiguous"));
				}
			}
			Openings.Add(Entrance.OpeningBoundsCm);
		}
	}
	TSharedPtr<FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5EntranceMask(MoveTemp(Lease)));
	Scratch->Plans.Append(Candidates.GetData(), Candidates.Num());
	Scratch->Authorized.SetNumUninitialized(static_cast<int32>(Outputs));
	for (int64 Z = 0; Z < Count.Z; ++Z)
	{
		for (int64 Y = 0; Y < Count.Y; ++Y)
		{
			for (int64 X = 0; X < Count.X; ++X)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
				}
				const int64 Index = X + int64(Count.X) * Y + OutputXY * Z;
				if (Protection[Index] > 1)
				{
					return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
						EVoxelLandM5ErrorCode::InvalidInput, TEXT("Protection mask is not binary"));
				}
				const int64 Point[3] = {OriginCm.X + (int64(Start.X) + X) * SpacingCm,
					OriginCm.Y + (int64(Start.Y) + Y) * SpacingCm, OriginCm.Z + (int64(Start.Z) + Z) * SpacingCm};
				bool bInsideGate = true;
				if (bGated)
					for (int32 Axis = 0; Axis < 3; ++Axis)
						bInsideGate &= Point[Axis] - Influence >= FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Settings.Bounds.MinInclusive, Axis)
							&& Point[Axis] + Influence < FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Settings.Bounds.MaxExclusive, Axis);
				bool bInside = false;
				for (const FVoxelLandM5Bounds& Opening : Openings)
				{
					if (IsCancelled())
					{
						return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
							EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
					}
					bool bFits = true;
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						bFits &= Point[Axis] - Influence
								  >= FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(Opening.MinInclusive, Axis)
							  && Point[Axis] + Influence < FVoxelLandM5EntranceMaskInternals::M5EntranceMaskAxis(
									 Opening.MaxExclusive, Axis);
					}
					bInside |= bFits;
				}
				Scratch->Authorized[Index] = bInsideGate && bInside && Protection[Index] == 0 ? 1 : 0;
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5EntranceMaskInternals::M5EntranceMaskError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Entrance mask cancelled"));
	}
	OutMask = MoveTemp(Scratch);
	return {};
}
