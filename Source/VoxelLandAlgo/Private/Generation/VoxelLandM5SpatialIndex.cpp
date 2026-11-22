#include "Generation/VoxelLandM5SpatialIndex.h"

#include <cmath>

namespace
{
FVoxelLandError M5SpatialIndexError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

bool M5ValidBounds(const FBox3d& Box)
{
	return Box.IsValid && std::isfinite(Box.Min.X) && std::isfinite(Box.Min.Y) && std::isfinite(Box.Min.Z)
		&& std::isfinite(Box.Max.X) && std::isfinite(Box.Max.Y) && std::isfinite(Box.Max.Z) && Box.Min.X <= Box.Max.X
		&& Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
}

bool M5IntersectsInclusive(const FBox3d& A, const FBox3d& B)
{
	return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X && A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y && A.Min.Z <= B.Max.Z
		&& A.Max.Z >= B.Min.Z;
}
}

FVoxelLandError FVoxelLandM5SpatialIndex::Build(TConstArrayView<FVoxelLandM5InfluenceBounds> Entries, int32 MaxEntries,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe>& OutIndex)
{
	if (MaxEntries < 0 || MaxEntries > MaximumEntries || Entries.Num() > MaxEntries)
	{
		return M5SpatialIndexError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index entry budget exceeded"));
	}
	// Validate before allocating the index. The hard cap bounds this exhaustive
	// duplicate check, and cancellation is observed inside the inner loop.
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
		}
		if (!M5ValidBounds(Entries[I].Bounds))
		{
			return M5SpatialIndexError(
				EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index contains invalid bounds"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
			}
			if (Entries[I].PrimitiveId == Entries[J].PrimitiveId)
			{
				return M5SpatialIndexError(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial index contains duplicate IDs"));
			}
		}
	}
	if (IsCancelled())
	{
		return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
	}
	TSharedPtr<FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Candidate = MakeShareable(new FVoxelLandM5SpatialIndex());
	if (Entries.Num() > 0)
	{
		Candidate->EntriesById.Append(Entries.GetData(), Entries.Num());
	}
	Candidate->EntriesById.Sort([](const FVoxelLandM5InfluenceBounds& A, const FVoxelLandM5InfluenceBounds& B) {
		return A.PrimitiveId < B.PrimitiveId;
	});
	if (IsCancelled())
	{
		return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial index build cancelled"));
	}
	OutIndex = MoveTemp(Candidate);
	return {};
}

FVoxelLandError FVoxelLandM5SpatialIndex::Query(
	const FBox3d& Bounds, TFunctionRef<bool()> IsCancelled, TArray<uint64>& OutIds) const
{
	if (!M5ValidBounds(Bounds))
	{
		return M5SpatialIndexError(EVoxelLandErrorCode::InvalidParameter, TEXT("M5 spatial query has invalid bounds"));
	}
	TArray<uint64> Candidate;
	for (int32 I = 0; I < EntriesById.Num(); ++I)
	{
		if ((I & 63) == 0 && IsCancelled())
		{
			return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
		}
		if (M5IntersectsInclusive(EntriesById[I].Bounds, Bounds))
		{
			Candidate.Add(EntriesById[I].PrimitiveId);
		}
	}
	if (IsCancelled())
	{
		return M5SpatialIndexError(EVoxelLandErrorCode::Cancelled, TEXT("M5 spatial query cancelled"));
	}
	OutIds = MoveTemp(Candidate);
	return {};
}
