#include "Generation/VoxelLandM5PortalHandshake.h"

#include "Data/Enums/VoxelLandM5PlanningMode.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Generation/VoxelLandM5RouteTopology.h"
#include "Generation/VoxelLandM5Topology.h"

class FVoxelLandM5PortalHandshakeInternals final
{
public:
	static constexpr int64 MaxSampleCoordinateCm = 1048576;
	static constexpr int64 CancellationPollStride = 64;
	static constexpr int64 DigestWorkAllowance = 8192;
	static constexpr int64 DigestScratchBytes = 4096;

	static FVoxelLandM5Diagnostic M5HandshakeError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("M5.PortalHandshake");
		D.Message = Message;
		return D;
	}

	static bool M5HandshakeSameRegion(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
	{
		return A.X == B.X && A.Y == B.Y;
	}

	static bool M5HandshakeSameKey(const FVoxelLandM5PortalKey& A, const FVoxelLandM5PortalKey& B)
	{
		return A.WorldSeed == B.WorldSeed && A.StageId == B.StageId && A.Salt == B.Salt && A.Axis == B.Axis
			&& M5HandshakeSameRegion(A.RegionA, B.RegionA) && M5HandshakeSameRegion(A.RegionB, B.RegionB);
	}

	static bool M5HandshakeSamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	static bool M5HandshakeBounds(const FVoxelLandM5PortalOverlap& V, int64 (&Last)[3], int64& Count)
	{
		if (V.SpacingCm <= 0 || V.Counts.X <= 0 || V.Counts.Y <= 0 || V.Counts.Z <= 0)
		{
			return false;
		}
		const int32 Sizes[3] = {V.Counts.X, V.Counts.Y, V.Counts.Z};
		const int64 Origins[3] = {V.OriginCm.X, V.OriginCm.Y, V.OriginCm.Z};
		int64 XY = 0;
		if (!VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(Sizes[0], Sizes[1], XY)
			|| !VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(XY, Sizes[2], Count) || Count != V.Samples.Num())
		{
			return false;
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			int64 Distance = 0;
			if (!VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(int64(Sizes[Axis]) - 1, V.SpacingCm, Distance)
				|| !VoxelLand::M5::CheckedMath::TryAdd(Origins[Axis], Distance, Last[Axis])
				|| Origins[Axis] < -MaxSampleCoordinateCm || Last[Axis] > MaxSampleCoordinateCm)
			{
				return false;
			}
		}
		return true;
	}
};

bool VoxelLand::M5::PortalHandshake::MatchPortalRecords(const FVoxelLandM5Portal& A, const FVoxelLandM5Portal& B)
{
	return A.bHasId && B.bHasId && FVoxelLandM5PortalHandshakeInternals::M5HandshakeSameKey(A.Key, B.Key)
		&& A.Id == B.Id && A.RadiusCm > 0 && A.RadiusCm == B.RadiusCm
		&& FVoxelLandM5PortalHandshakeInternals::M5HandshakeSamePoint(A.CenterCm, B.CenterCm)
		&& A.bHasRegionAFeetAnchor == B.bHasRegionAFeetAnchor
		&& (!A.bHasRegionAFeetAnchor
			|| FVoxelLandM5PortalHandshakeInternals::M5HandshakeSamePoint(A.RegionAFeetAnchorCm, B.RegionAFeetAnchorCm))
		&& A.bHasRegionBFeetAnchor == B.bHasRegionBFeetAnchor
		&& (!A.bHasRegionBFeetAnchor
			|| FVoxelLandM5PortalHandshakeInternals::M5HandshakeSamePoint(
				A.RegionBFeetAnchorCm, B.RegionBFeetAnchorCm));
}

FVoxelLandM5Diagnostic VoxelLand::M5::PortalHandshake::Check(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& RegionA, const FVoxelLandM5RegionCoord& RegionB,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> A,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> B, TConstArrayView<uint8> ExpectedDescriptor,
	TConstArrayView<uint8> DescriptorA, TConstArrayView<uint8> DescriptorB, int32 VoxelSizeCm, int32 HaloSamples,
	const FVoxelLandM5PortalOverlap& SamplesA, const FVoxelLandM5PortalOverlap& SamplesB,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Handshake cancelled"));
	}
	if (!A.IsValid() || !B.IsValid())
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::ProviderUnavailable, TEXT("A neighboring plan is unavailable"));
	}
	int64 BudgetBytes = 0, BudgetWork = 0;
	Budget->GetLimits(BudgetBytes, BudgetWork);
	if (Settings.PlanningMode != EVoxelLandM5PlanningMode::Regional || !Settings.bEnabled || Settings.RegionSizeCm <= 0
		|| VoxelSizeCm <= 0 || HaloSamples < 1 || HaloSamples > 1024 || Settings.RegionSizeCm % VoxelSizeCm != 0
		|| BudgetBytes > Settings.Limits.MaxBytes || BudgetWork > Settings.Limits.MaxValidationWork
		|| ExpectedDescriptor.IsEmpty() || ExpectedDescriptor.Num() > 4 * 1024 * 1024 || !A->UsesBudget(Budget.Get())
		|| !B->UsesBudget(Budget.Get()) || !A->MatchesSource(Settings, WorldSeed, RegionA)
		|| !B->MatchesSource(Settings, WorldSeed, RegionB))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Incompatible regional inputs or budget"));
	}
	const bool bRouted = !A->GetPlan().Junctions.IsEmpty() || !B->GetPlan().Junctions.IsEmpty()
					  || !A->GetPlan().Routes.IsEmpty() || !B->GetPlan().Routes.IsEmpty();
	int64 AdjacentX = 0, AdjacentY = 0;
	const bool bX = RegionA.Y == RegionB.Y
				 && ((VoxelLand::M5::CheckedMath::TryAdd(RegionA.X, 1, AdjacentX) && AdjacentX == RegionB.X)
					 || (VoxelLand::M5::CheckedMath::TryAdd(RegionB.X, 1, AdjacentX) && AdjacentX == RegionA.X));
	const bool bY = RegionA.X == RegionB.X
				 && ((VoxelLand::M5::CheckedMath::TryAdd(RegionA.Y, 1, AdjacentY) && AdjacentY == RegionB.Y)
					 || (VoxelLand::M5::CheckedMath::TryAdd(RegionB.Y, 1, AdjacentY) && AdjacentY == RegionA.Y));
	if (bX == bY)
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Regions are not face-adjacent"));
	}
	int64 LastA[3], LastB[3], CountA = 0, CountB = 0;
	if (!FVoxelLandM5PortalHandshakeInternals::M5HandshakeBounds(SamplesA, LastA, CountA)
		|| !FVoxelLandM5PortalHandshakeInternals::M5HandshakeBounds(SamplesB, LastB, CountB)
		|| SamplesA.SpacingCm != VoxelSizeCm || SamplesB.SpacingCm != VoxelSizeCm || SamplesA.Counts != SamplesB.Counts
		|| SamplesA.OriginCm.X != SamplesB.OriginCm.X || SamplesA.OriginCm.Y != SamplesB.OriginCm.Y
		|| SamplesA.OriginCm.Z != SamplesB.OriginCm.Z)
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Invalid or mismatched overlap blocks"));
	}
	const int64 BlockOrigin[3] = {SamplesA.OriginCm.X, SamplesA.OriginCm.Y, SamplesA.OriginCm.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (BlockOrigin[Axis] % VoxelSizeCm != 0)
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Overlap is not aligned to the descriptor lattice"));
		}
	}
	const int64 PortalCount = int64(A->GetPlan().Portals.Num()) + B->GetPlan().Portals.Num();
	int64 Work = 0, DescriptorWork = 0, PortalWork = 0, Polls = 0;
	// Two descriptor comparisons per byte, one density comparison per sample,
	// one cancellation callback per 64 entries, one per portal, and a fixed
	// allowance for two bounded SHA-256 packet/digest computations.
	if (!VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(ExpectedDescriptor.Num(), 2, DescriptorWork)
		|| !VoxelLand::M5::CheckedMath::TryAdd(
			(int64(ExpectedDescriptor.Num()) + FVoxelLandM5PortalHandshakeInternals::CancellationPollStride - 1)
				/ FVoxelLandM5PortalHandshakeInternals::CancellationPollStride,
			(CountA + FVoxelLandM5PortalHandshakeInternals::CancellationPollStride - 1)
				/ FVoxelLandM5PortalHandshakeInternals::CancellationPollStride,
			Polls)
		|| !VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(PortalCount, 2, PortalWork)
		|| !VoxelLand::M5::CheckedMath::TryAdd(CountA, DescriptorWork, Work)
		|| !VoxelLand::M5::CheckedMath::TryAdd(Work, PortalWork, Work)
		|| !VoxelLand::M5::CheckedMath::TryAdd(Work, Polls, Work)
		|| !VoxelLand::M5::CheckedMath::TryAdd(Work, FVoxelLandM5PortalHandshakeInternals::DigestWorkAllowance, Work)
		|| !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Handshake work budget exhausted"));
	}
	// Covers two digest arrays, temporary encoded packets and bounded native
	// bookkeeping; allocator overhead is outside the budget's payload counter.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> DigestLease;
	if (!Budget->TryReserveBytes(FVoxelLandM5PortalHandshakeInternals::DigestScratchBytes, DigestLease))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Handshake scratch budget exhausted"));
	}
	if (ExpectedDescriptor.Num() != DescriptorA.Num() || ExpectedDescriptor.Num() != DescriptorB.Num())
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Regional descriptor mismatch"));
	}
	for (int32 I = 0; I < ExpectedDescriptor.Num(); ++I)
	{
		if ((I & 63) == 0 && IsCancelled())
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Handshake cancelled"));
		}
		if (ExpectedDescriptor[I] != DescriptorA[I] || ExpectedDescriptor[I] != DescriptorB[I])
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Regional descriptor mismatch"));
		}
	}
	FVoxelLandM5Diagnostic D =
		bRouted ? VoxelLand::M5::RouteTopology::ValidateStructure(
					  Settings, WorldSeed, RegionA, A.ToSharedRef(), Budget, IsCancelled)
				: VoxelLand::M5::Topology::Validate(Settings, WorldSeed, RegionA, A.ToSharedRef(), Budget, IsCancelled);
	if (D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	D = bRouted ? VoxelLand::M5::RouteTopology::ValidateStructure(
					  Settings, WorldSeed, RegionB, B.ToSharedRef(), Budget, IsCancelled)
				: VoxelLand::M5::Topology::Validate(Settings, WorldSeed, RegionB, B.ToSharedRef(), Budget, IsCancelled);
	if (D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	const FVoxelLandM5Portal* FoundA = nullptr;
	const FVoxelLandM5Portal* FoundB = nullptr;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVoxelLandM5CavePlan& Plan = Side == 0 ? A->GetPlan() : B->GetPlan();
		const FVoxelLandM5RegionCoord& Other = Side == 0 ? RegionB : RegionA;
		for (const FVoxelLandM5Portal& P : Plan.Portals)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Handshake cancelled"));
			}
			if (FVoxelLandM5PortalHandshakeInternals::M5HandshakeSameRegion(P.Key.RegionA, Other)
				|| FVoxelLandM5PortalHandshakeInternals::M5HandshakeSameRegion(P.Key.RegionB, Other))
			{
				const FVoxelLandM5Portal*& Found = Side == 0 ? FoundA : FoundB;
				if (Found != nullptr)
				{
					return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
						EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Multiple face portals are unsupported"));
				}
				Found = &P;
			}
		}
	}
	if (FoundA == nullptr || FoundB == nullptr)
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::Blocked, TEXT("A neighboring portal is missing"));
	}
	if (!MatchPortalRecords(*FoundA, *FoundB)
		|| (bX ? FoundA->Key.Axis != EVoxelLandM5PortalAxis::X : FoundA->Key.Axis != EVoxelLandM5PortalAxis::Y))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Neighboring portal geometry or key differs"));
	}
	if (bRouted && (!FoundA->bHasRegionAFeetAnchor || !FoundA->bHasRegionBFeetAnchor))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Both regional feet anchors are required"));
	}
	int64 FaceOffset = 0, Face = 0;
	const int64 Higher = bX ? FMath::Max(RegionA.X, RegionB.X) : FMath::Max(RegionA.Y, RegionB.Y);
	const int64 GridOrigin = bX ? Settings.RegionOriginXCm : Settings.RegionOriginYCm;
	if (!VoxelLand::M5::CheckedMath::TryMultiply(Higher, Settings.RegionSizeCm, FaceOffset)
		|| !VoxelLand::M5::CheckedMath::TryAdd(GridOrigin, FaceOffset, Face)
		|| Face != (bX ? FoundA->CenterCm.X : FoundA->CenterCm.Y))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Portal is not on the shared face"));
	}
	TArray<uint8> DigestA, DigestB;
	const FVoxelLandM5PortalKey& K = FoundA->Key;
	if (!VoxelLand::M5::Seed::TryPortalDigest(K.WorldSeed, K.StageId, static_cast<uint64>(K.Salt), K.RegionA.X,
			K.RegionA.Y, K.RegionB.X, K.RegionB.Y, DigestA)
		|| !VoxelLand::M5::Seed::TryPortalDigest(FoundB->Key.WorldSeed, FoundB->Key.StageId,
			static_cast<uint64>(FoundB->Key.Salt), FoundB->Key.RegionA.X, FoundB->Key.RegionA.Y, FoundB->Key.RegionB.X,
			FoundB->Key.RegionB.Y, DigestB)
		|| DigestA != DigestB)
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Portal full digest mismatch"));
	}
	const int64 Center[3] = {FoundA->CenterCm.X, FoundA->CenterCm.Y, FoundA->CenterCm.Z};
	const int64 Origin[3] = {SamplesA.OriginCm.X, SamplesA.OriginCm.Y, SamplesA.OriginCm.Z};
	int64 HaloCm = 0, ExpandedRadius = 0;
	if (!VoxelLand::M5::CheckedMath::TryMultiplyNonNegative(HaloSamples, VoxelSizeCm, HaloCm)
		|| !VoxelLand::M5::CheckedMath::TryAdd(FoundA->RadiusCm, HaloCm, ExpandedRadius))
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Portal halo overflow"));
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int64 Min = 0, Max = 0;
		if (!VoxelLand::M5::CheckedMath::TrySubtract(Center[Axis], ExpandedRadius, Min)
			|| !VoxelLand::M5::CheckedMath::TryAdd(Center[Axis], ExpandedRadius, Max) || Origin[Axis] > Min
			|| LastA[Axis] < Max)
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Overlap does not cover the portal geometry"));
		}
	}
	const int32 FaceAxis = bX ? 0 : 1;
	int64 Delta = 0;
	if (!VoxelLand::M5::CheckedMath::TrySubtract(Center[FaceAxis], Origin[FaceAxis], Delta)
		|| Delta % SamplesA.SpacingCm != 0)
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Overlap has no samples on the shared face"));
	}
	for (int32 I = 0; I < SamplesA.Samples.Num(); ++I)
	{
		if ((I & 63) == 0 && IsCancelled())
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Handshake cancelled"));
		}
		if (SamplesA.Samples[I] != SamplesB.Samples[I])
		{
			return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
				EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Overlap samples differ"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5PortalHandshakeInternals::M5HandshakeError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Handshake cancelled"));
	}
	return {};
}
