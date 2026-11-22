#include "Generation/VoxelLandM5RegionClosure.h"

#include "Data/Enums/VoxelLandM5PlanningMode.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5PortalHandshake.h"
#include "Generation/VoxelLandM5Topology.h"

namespace
{
using namespace VoxelLand::M5::CheckedMath;
constexpr int32 MaxClosureRegions = 256;

FVoxelLandM5Diagnostic ClosureError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic D;
	D.Code = Code;
	D.Stage = TEXT("M5.RegionClosure");
	D.Message = Message;
	return D;
}

bool Same(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X == B.X && A.Y == B.Y;
}

bool Less(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X != B.X ? A.X < B.X : A.Y < B.Y;
}

bool Adjacent(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	int64 Next = 0;
	return (A.Y == B.Y && ((TryAdd(A.X, 1, Next) && Next == B.X) || (TryAdd(B.X, 1, Next) && Next == A.X)))
		|| (A.X == B.X && ((TryAdd(A.Y, 1, Next) && Next == B.Y) || (TryAdd(B.Y, 1, Next) && Next == A.Y)));
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::RegionClosure::Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<FVoxelLandM5RegionCoord> Required, TConstArrayView<FVoxelLandM5RegionInput> Regions,
	TConstArrayView<FVoxelLandM5RegionEdgeInput> Edges, TConstArrayView<uint8> ExpectedDescriptor, int32 VoxelSizeCm,
	int32 HaloSamples, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TConstArrayView<uint8>, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>)>
		VerifyDescriptor,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe>& OutClosure)
{
	if (IsCancelled())
	{
		return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
	}
	const int32 N = Required.Num();
	int64 MaxBytes = 0, MaxWork = 0;
	Budget->GetLimits(MaxBytes, MaxWork);
	if (!Settings.bEnabled || Settings.PlanningMode != EVoxelLandM5PlanningMode::Regional || N < 1
		|| N > MaxClosureRegions || Regions.Num() != N || Edges.Num() > 2 * N || ExpectedDescriptor.IsEmpty()
		|| ExpectedDescriptor.Num() > 4 * 1024 * 1024 || VoxelSizeCm <= 0 || HaloSamples < 1
		|| MaxBytes > Settings.Limits.MaxBytes || MaxWork > Settings.Limits.MaxValidationWork)
	{
		return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid region closure inputs or budget"));
	}
	// Bound membership, sorting, edge matching, connectivity and descriptor
	// comparisons. Handshakes and descriptor providers charge their own work.
	int64 PairWork = 0, DescriptorWork = 0, Work = 0, ResultBytes = 0, ScratchBytes = 0;
	if (!TryMultiplyNonNegative(N, N, PairWork) || !TryMultiplyNonNegative(PairWork, int64(Edges.Num()) + 1, PairWork)
		|| !TryMultiplyNonNegative(N, ExpectedDescriptor.Num(), DescriptorWork)
		|| !TryAdd(PairWork, DescriptorWork, Work) || !TryAdd(Work, int64(N) * 8 + Edges.Num() + 128, Work)
		|| !TryMultiplyNonNegative(Work, 16, Work)
		|| !TryMultiplyNonNegative(N,
			int64(sizeof(FVoxelLandM5RegionCoord))
				+ int64(sizeof(TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>)),
			ResultBytes)
		|| !TryAdd(ResultBytes, ExpectedDescriptor.Num(), ResultBytes)
		|| !TryMultiplyNonNegative(ResultBytes, 4, ResultBytes) || !TryAdd(ResultBytes, 4096, ResultBytes)
		|| !TryMultiplyNonNegative(N, int64(sizeof(int32)) * 4, ScratchBytes)
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes) || !Budget->TryChargeWork(Work))
	{
		return ClosureError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Region closure work or size exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ResultLease, ScratchLease;
	if (!Budget->TryReserveBytes(ResultBytes, ResultLease) || !Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return ClosureError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Region closure memory budget exhausted"));
	}
	FVoxelLandM5Diagnostic D = VerifyDescriptor(ExpectedDescriptor, Budget);
	if (D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	TArray<int32> Order;
	Order.Reserve(N);
	for (int32 I = 0; I < N; ++I)
	{
		if (IsCancelled())
		{
			return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
		}
		int32 Match = INDEX_NONE;
		for (int32 J = 0; J < N; ++J)
		{
			if (Same(Required[I], Regions[J].Region))
			{
				if (Match != INDEX_NONE)
				{
					return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate supplied region"));
				}
				Match = J;
			}
		}
		if (Match == INDEX_NONE || !Regions[Match].Candidate.IsValid())
		{
			return ClosureError(EVoxelLandM5ErrorCode::ProviderUnavailable, TEXT("Required region unavailable"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if (Same(Required[I], Required[J]))
			{
				return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate required region"));
			}
		}
		const FVoxelLandM5RegionInput& Input = Regions[Match];
		if (!Input.Candidate->UsesBudget(Budget.Get())
			|| !Input.Candidate->MatchesSource(Settings, WorldSeed, Input.Region)
			|| Input.Descriptor.Num() != ExpectedDescriptor.Num())
		{
			return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Region provenance mismatch"));
		}
		for (int32 J = 0; J < ExpectedDescriptor.Num(); ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
			}
			if (Input.Descriptor[J] != ExpectedDescriptor[J])
			{
				return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Region descriptor mismatch"));
			}
		}
		Order.Add(Match);
	}
	// Canonical region order avoids depending on request order.
	for (int32 I = 1; I < N; ++I)
	{
		const int32 Item = Order[I];
		int32 J = I;
		while (J > 0 && Less(Regions[Item].Region, Regions[Order[J - 1]].Region))
		{
			if (IsCancelled())
			{
				return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
			}
			Order[J] = Order[J - 1];
			--J;
		}
		Order[J] = Item;
	}
	TArray<int32> Parent;
	Parent.Reserve(N);
	for (int32 I = 0; I < N; ++I)
	{
		Parent.Add(I);
	}
	int32 Handshakes = 0;
	for (int32 I = 0; I < N; ++I)
	{
		for (int32 J = I + 1; J < N; ++J)
		{
			if (IsCancelled())
			{
				return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
			}
			const FVoxelLandM5RegionInput& A = Regions[Order[I]];
			const FVoxelLandM5RegionInput& B = Regions[Order[J]];
			if (!Adjacent(A.Region, B.Region))
			{
				continue;
			}
			int32 Match = INDEX_NONE;
			bool bReverse = false;
			for (int32 K = 0; K < Edges.Num(); ++K)
			{
				const bool Forward = Same(Edges[K].A, A.Region) && Same(Edges[K].B, B.Region);
				const bool Reverse = Same(Edges[K].B, A.Region) && Same(Edges[K].A, B.Region);
				if (Forward || Reverse)
				{
					if (Match != INDEX_NONE)
					{
						return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate regional overlap"));
					}
					Match = K;
					bReverse = Reverse;
				}
			}
			if (Match == INDEX_NONE)
			{
				return ClosureError(
					EVoxelLandM5ErrorCode::ProviderUnavailable, TEXT("Required regional overlap missing"));
			}
			const FVoxelLandM5RegionEdgeInput& Edge = Edges[Match];
			D = PortalHandshake::Check(Settings, WorldSeed, A.Region, B.Region, A.Candidate, B.Candidate,
				ExpectedDescriptor, A.Descriptor, B.Descriptor, VoxelSizeCm, HaloSamples,
				bReverse ? Edge.SamplesB : Edge.SamplesA, bReverse ? Edge.SamplesA : Edge.SamplesB, Budget,
				IsCancelled);
			if (D.Code != EVoxelLandM5ErrorCode::None)
			{
				return D;
			}
			++Handshakes;
			int32 RootA = I, RootB = J;
			while (Parent[RootA] != RootA)
			{
				RootA = Parent[RootA];
			}
			while (Parent[RootB] != RootB)
			{
				RootB = Parent[RootB];
			}
			Parent[RootB] = RootA;
		}
	}
	if (Handshakes != Edges.Num())
	{
		return ClosureError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Unexpected or duplicate overlap"));
	}
	for (int32 I = 1; I < N; ++I)
	{
		int32 RootA = 0, RootB = I;
		while (Parent[RootA] != RootA)
		{
			RootA = Parent[RootA];
		}
		while (Parent[RootB] != RootB)
		{
			RootB = Parent[RootB];
		}
		if (RootA != RootB)
		{
			return ClosureError(EVoxelLandM5ErrorCode::Blocked, TEXT("Required region set is disconnected"));
		}
	}
	if (N == 1)
	{
		const FVoxelLandM5RegionInput& Only = Regions[Order[0]];
		D = Topology::Validate(Settings, WorldSeed, Only.Region, Only.Candidate.ToSharedRef(), Budget, IsCancelled);
		if (D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
	}
	D = VerifyDescriptor(ExpectedDescriptor, Budget);
	if (D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	if (IsCancelled())
	{
		return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
	}
	TArray<FVoxelLandM5RegionCoord> SortedRegions;
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates;
	TArray<uint8> Descriptor;
	SortedRegions.Reserve(N);
	Candidates.Reserve(N);
	Descriptor.Append(ExpectedDescriptor.GetData(), ExpectedDescriptor.Num());
	for (int32 Index : Order)
	{
		SortedRegions.Add(Regions[Index].Region);
		Candidates.Add(Regions[Index].Candidate);
	}
	TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5RegionClosure(
			Budget, MoveTemp(ResultLease), MoveTemp(SortedRegions), MoveTemp(Candidates), MoveTemp(Descriptor)));
	if (IsCancelled())
	{
		return ClosureError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Region closure cancelled"));
	}
	OutClosure = MoveTemp(Result);
	return {};
}
