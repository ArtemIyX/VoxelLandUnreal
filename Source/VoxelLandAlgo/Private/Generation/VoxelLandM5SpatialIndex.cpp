#include "Generation/VoxelLandM5SpatialIndex.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5SpatialQueryResult.h"

#include <cmath>

class FVoxelLandM5SpatialIndexInternals final
{
public:
	static FVoxelLandError M5SpatialIndexError(EVoxelLandErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandError Error;
		Error.Code = Code;
		Error.Message = Message;
		return Error;
	}

	static bool M5ValidBounds(const FBox3d& Box)
	{
		return Box.IsValid && std::isfinite(Box.Min.X) && std::isfinite(Box.Min.Y) && std::isfinite(Box.Min.Z)
			&& std::isfinite(Box.Max.X) && std::isfinite(Box.Max.Y) && std::isfinite(Box.Max.Z)
			&& Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	static bool M5IntersectsInclusive(const FBox3d& A, const FBox3d& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X && A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y
			&& A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
};

FVoxelLandM5SpatialIndex::FVoxelLandM5SpatialIndex(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Budget(MoveTemp(InBudget))
	, Lease(MoveTemp(InLease))
{
}

FVoxelLandError FVoxelLandM5SpatialIndex::Build(TConstArrayView<FVoxelLandM5InfluenceBounds> Entries, int32 MaxEntries,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe>& OutIndex)
{
	if (MaxEntries < 0 || MaxEntries > MaximumEntries || Entries.Num() > MaxEntries)
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index entry budget exceeded"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
	}
	using namespace VoxelLand::M5::CheckedMath;
	const int64 Count = Entries.Num();
	int64 Bytes = 0, Work = 0, Square = 0, Linear = 0;
	// Fourfold payload slack plus a fixed 16 KiB is a conservative accounting
	// estimate for index/control blocks and array capacity. Allocator overhead
	// and caller-owned input memory are not measured or proven bounded here.
	// A square bound covers both duplicate comparisons and worst-case sorting.
	if (!TryMultiplyNonNegative(Count, int64(sizeof(FVoxelLandM5InfluenceBounds)), Bytes)
		|| !TryMultiplyNonNegative(Bytes, 4, Bytes) || !TryAdd(Bytes, 16384, Bytes)
		|| !TryMultiplyNonNegative(Count, Count, Square) || !TryMultiplyNonNegative(Square, 2, Work)
		|| !TryMultiplyNonNegative(Count, 8, Linear) || !TryAdd(Work, Linear, Work) || !TryAdd(Work, 16, Work))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index admission overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Admission;
	if (!Budget->TryReserveBytes(Bytes, Admission))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index memory budget exhausted"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index work budget exhausted"));
	}
	// Validate before allocating the index. The hard cap bounds this exhaustive
	// duplicate check, and cancellation is observed inside the inner loop.
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
				EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
		}
		if (!FVoxelLandM5SpatialIndexInternals::M5ValidBounds(Entries[I].Bounds))
		{
			return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
				EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index contains invalid bounds"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
					EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
			}
			if (Entries[I].PrimitiveId == Entries[J].PrimitiveId)
			{
				return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index contains duplicate IDs"));
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
	}
	TSharedPtr<FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Candidate =
		MakeShareable(new FVoxelLandM5SpatialIndex(Budget, MoveTemp(Admission)));
	if (Entries.Num() > 0)
	{
		Candidate->EntriesById.Append(Entries.GetData(), Entries.Num());
	}
	Candidate->EntriesById.Sort([](const FVoxelLandM5InfluenceBounds& A, const FVoxelLandM5InfluenceBounds& B) {
		return A.PrimitiveId < B.PrimitiveId;
	});
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
	}
	OutIndex = MoveTemp(Candidate);
	return {};
}

FVoxelLandError FVoxelLandM5SpatialIndex::Query(const FBox3d& Bounds, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe>& OutResult) const
{
	if (!FVoxelLandM5SpatialIndexInternals::M5ValidBounds(Bounds))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial query has invalid bounds"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
	}
	using namespace VoxelLand::M5::CheckedMath;
	int64 Bytes = 0, Work = 0;
	if (!TryMultiplyNonNegative(EntriesById.Num(), int64(sizeof(uint64)), Bytes)
		|| !TryMultiplyNonNegative(Bytes, 4, Bytes) || !TryAdd(Bytes, 4096, Bytes)
		|| !TryMultiplyNonNegative(EntriesById.Num(), 4, Work) || !TryAdd(Work, 16, Work))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial query admission overflow"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Admission;
	if (!Budget->TryReserveBytes(Bytes, Admission))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial query memory budget exhausted"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial query work budget exhausted"));
	}
	TArray<uint64> Candidate;
	for (int32 I = 0; I < EntriesById.Num(); ++I)
	{
		if ((I & 63) == 0 && IsCancelled())
		{
			return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
				EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
		}
		if (FVoxelLandM5SpatialIndexInternals::M5IntersectsInclusive(EntriesById[I].Bounds, Bounds))
		{
			Candidate.Add(EntriesById[I].PrimitiveId);
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
	}
	TSharedPtr<FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5SpatialQueryResult(MoveTemp(Admission), MoveTemp(Candidate)));
	if (IsCancelled())
	{
		return FVoxelLandM5SpatialIndexInternals::M5SpatialIndexError(
			EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
	}
	OutResult = MoveTemp(Result);
	return {};
}
