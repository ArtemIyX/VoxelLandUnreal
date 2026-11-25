#include "Generation/Caves/Geometry/VoxelLandCaveFeetChain.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFeetProposalBuilder.h"

class FVoxelLandM5FeetChainInternals final
{
public:
	static FVoxelLandM5Diagnostic M5ChainError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = Code;
		Error.Stage = TEXT("M5.FeetChain");
		Error.Message = Message;
		return Error;
	}

	static bool M5ChainSame(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5FeetChain::Build(TConstArrayView<FVoxelLandM5Segment> Segments, uint64 ConnectionId,
	bool bReverseFirst, const FVoxelLandM5CapsuleProfile& Capsule, int32 MaxSegments,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe>& Out)
{
	if (IsCancelled())
	{
		return FVoxelLandM5FeetChainInternals::M5ChainError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet chain cancelled"));
	}
	if (MaxSegments < 1 || MaxSegments > 8192 || Segments.IsEmpty() || Segments.Num() > MaxSegments)
	{
		return FVoxelLandM5FeetChainInternals::M5ChainError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid segment count or limit"));
	}
	// Bound output plus TArray/control-block slack. The cap above guarantees
	// every product is representable; caller accounts for its borrowed input.
	const int64 Count = Segments.Num();
	const int64 Bytes = 4096 + 4 * Count * int64(sizeof(FVoxelLandM5FeetChainLeg));
	const int64 Work = 64 + 4 * Count * Count + 8 * Count;
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5FeetChainInternals::M5ChainError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Feet chain work limit exceeded"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5FeetChainInternals::M5ChainError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Feet chain memory limit exceeded"));
	}
	TArray<FVoxelLandM5FeetChainLeg> Scratch;
	Scratch.Reserve(Segments.Num());
	FVoxelLandM5PointCm Previous;
	for (int32 I = 0; I < Segments.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FeetChainInternals::M5ChainError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet chain cancelled"));
		}
		const FVoxelLandM5Segment& Input = Segments[I];
		if (!Input.bHasId || !Input.bHasConnectionId || Input.ConnectionId != ConnectionId)
		{
			return FVoxelLandM5FeetChainInternals::M5ChainError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Missing ID or wrong connection"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5FeetChainInternals::M5ChainError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet chain cancelled"));
			}
			if (Segments[J].Id == Input.Id)
			{
				return FVoxelLandM5FeetChainInternals::M5ChainError(
					EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate segment ID"));
			}
		}
		FVoxelLandM5Segment Oriented = Input;
		if ((I == 0 && bReverseFirst) || (I > 0 && FVoxelLandM5FeetChainInternals::M5ChainSame(Previous, Input.EndCm)))
		{
			Swap(Oriented.StartCm, Oriented.EndCm);
		}
		if (I > 0 && !FVoxelLandM5FeetChainInternals::M5ChainSame(Previous, Oriented.StartCm))
		{
			return FVoxelLandM5FeetChainInternals::M5ChainError(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Segment chain is disconnected"));
		}
		FVoxelLandM5FeetChainLeg Leg;
		Leg.SegmentId = Input.Id;
		const FVoxelLandM5Diagnostic Error =
			VoxelLand::M5::FeetProposalBuilder::Build(Oriented, Capsule, Budget, IsCancelled, Leg.Feet);
		if (Error.Code != EVoxelLandM5ErrorCode::None)
		{
			return Error;
		}
		if (I > 0 && !FVoxelLandM5FeetChainInternals::M5ChainSame(Scratch.Last().Feet.EndFeetCm, Leg.Feet.StartFeetCm))
		{
			return FVoxelLandM5FeetChainInternals::M5ChainError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Feet proposals do not meet"));
		}
		Scratch.Add(MoveTemp(Leg));
		Previous = Oriented.EndCm;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FeetChainInternals::M5ChainError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Feet chain cancelled"));
	}
	TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe> Complete =
		MakeShareable(new FVoxelLandM5FeetChain(MoveTemp(Lease), MoveTemp(Scratch)));
	Out = MoveTemp(Complete);
	return FVoxelLandM5Diagnostic();
}
