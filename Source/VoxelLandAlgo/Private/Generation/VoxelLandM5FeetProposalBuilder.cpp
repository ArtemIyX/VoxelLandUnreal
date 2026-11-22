#include "Generation/VoxelLandM5FeetProposalBuilder.h"

#include "Generation/VoxelLandM5Budget.h"

class FVoxelLandM5FeetProposalBuilderInternals final
{
public:
	static constexpr int64 M5FeetCoordinateLimit = 1048576;

	static FVoxelLandM5Diagnostic M5FeetError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FeetProposal");
		Result.Message = Message;
		return Result;
	}

	static bool M5FeetInside(int64 Value) { return Value >= -M5FeetCoordinateLimit && Value <= M5FeetCoordinateLimit; }

	static bool M5FeetPointInside(const FVoxelLandM5PointCm& P)
	{
		return M5FeetInside(P.X) && M5FeetInside(P.Y) && M5FeetInside(P.Z);
	}

	static bool M5FeetEnvelopeInside(const FVoxelLandM5PointCm& P, int64 RadiusWithMargin, int64 Height, int64 Margin)
	{
		// Point and profile validation bound every intermediate to below 2^23.
		return M5FeetInside(P.X - RadiusWithMargin) && M5FeetInside(P.X + RadiusWithMargin)
			&& M5FeetInside(P.Y - RadiusWithMargin) && M5FeetInside(P.Y + RadiusWithMargin)
			&& M5FeetInside(P.Z - Margin) && M5FeetInside(P.Z + Height + Margin);
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::FeetProposalBuilder::Build(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5FeetProposal& Out)
{
	if (IsCancelled())
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet proposal cancelled"));
	}
	const int64 Height = Capsule.StandingHeightCm;
	const int64 Radius = Capsule.RadiusCm;
	const int64 Margin = Capsule.ClearanceMarginCm;
	if (!Segment.bHasId || Segment.RadiusCm < 1
		|| Segment.RadiusCm > FVoxelLandM5FeetProposalBuilderInternals::M5FeetCoordinateLimit || Height < 1
		|| Height > FVoxelLandM5FeetProposalBuilderInternals::M5FeetCoordinateLimit || Radius < 1
		|| Radius > FVoxelLandM5FeetProposalBuilderInternals::M5FeetCoordinateLimit || Margin < 0
		|| Margin > FVoxelLandM5FeetProposalBuilderInternals::M5FeetCoordinateLimit || Height < 2 * Radius
		|| Capsule.MaxSlopeDegrees < 0 || Capsule.MaxSlopeDegrees > 90 || Capsule.MaxStepCm < 0)
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid segment or capsule profile"));
	}
	if (!FVoxelLandM5FeetProposalBuilderInternals::M5FeetPointInside(Segment.StartCm)
		|| !FVoxelLandM5FeetProposalBuilderInternals::M5FeetPointInside(Segment.EndCm))
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Segment lies outside the supported domain"));
	}
	if (!Budget->TryChargeWork(32))
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Feet proposal work budget exceeded"));
	}
	const int64 DX = Segment.EndCm.X - Segment.StartCm.X;
	const int64 DY = Segment.EndCm.Y - Segment.StartCm.Y;
	const int64 DZ = Segment.EndCm.Z - Segment.StartCm.Z;
	if (DX == 0 && DY == 0 && DZ == 0)
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Zero-length segment"));
	}
	// The capsule, expanded by Margin, is contained by a ball of radius
	// ceil(Height/2)+Margin centered at each integer segment center. Sweeping
	// those balls is a subset of the constant-radius ideal segment volume.
	if ((Height + 1) / 2 + Margin > Segment.RadiusCm)
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Capsule envelope does not fit the ideal segment"));
	}
	const int64 HorizontalLowerBound = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
	// tan(theta) >= theta / 90 for theta in [0,90) degrees; max(|dx|,|dy|)
	// is no larger than horizontal length. The 90-degree case uses the same
	// conservative bound rather than attempting an infinite tangent.
	if (90 * FMath::Abs(DZ) > int64(Capsule.MaxSlopeDegrees) * HorizontalLowerBound)
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Centerline slope is not conservatively walkable"));
	}
	FVoxelLandM5FeetProposal Scratch;
	Scratch.StartFeetCm = Segment.StartCm;
	Scratch.EndFeetCm = Segment.EndCm;
	Scratch.StartFeetCm.Z -= Height / 2;
	Scratch.EndFeetCm.Z -= Height / 2;
	if (!FVoxelLandM5FeetProposalBuilderInternals::M5FeetEnvelopeInside(
			Scratch.StartFeetCm, Radius + Margin, Height, Margin)
		|| !FVoxelLandM5FeetProposalBuilderInternals::M5FeetEnvelopeInside(
			Scratch.EndFeetCm, Radius + Margin, Height, Margin))
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Capsule envelope lies outside the supported domain"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FeetProposalBuilderInternals::M5FeetError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet proposal cancelled"));
	}
	Out = Scratch;
	return FVoxelLandM5Diagnostic();
}
