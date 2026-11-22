#pragma once

#include "Data/Enums/VoxelLandWorldKind.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Structs/VoxelLandInfiniteWorldSnapshot.h"
#include "Misc/TVariant.h"

// Native authoring capture. The stored alternative is the world kind.
// This is an in-memory input to validation, with no persistence contract.
struct FVoxelLandWorldSourceSnapshot
{
	explicit FVoxelLandWorldSourceSnapshot(FVoxelLandFiniteWorldSnapshot&& Snapshot)
		: Source(TInPlaceType<FVoxelLandFiniteWorldSnapshot>(), MoveTemp(Snapshot))
	{
	}

	explicit FVoxelLandWorldSourceSnapshot(FVoxelLandInfiniteWorldSnapshot&& Snapshot)
		: Source(TInPlaceType<FVoxelLandInfiniteWorldSnapshot>(), MoveTemp(Snapshot))
	{
	}

	EVoxelLandWorldKind GetWorldKind() const
	{
		return GetFinite() ? EVoxelLandWorldKind::Finite : EVoxelLandWorldKind::Infinite;
	}

	const FVoxelLandFiniteWorldSnapshot* GetFinite() const { return Source.TryGet<FVoxelLandFiniteWorldSnapshot>(); }

	const FVoxelLandInfiniteWorldSnapshot* GetInfinite() const
	{
		return Source.TryGet<FVoxelLandInfiniteWorldSnapshot>();
	}

	const FVoxelLandWorldCommonSnapshot& GetCommon() const
	{
		if (const FVoxelLandFiniteWorldSnapshot* Finite = GetFinite())
		{
			return Finite->Common;
		}
		return Source.Get<FVoxelLandInfiniteWorldSnapshot>().Common;
	}

private:
	TVariant<FVoxelLandFiniteWorldSnapshot, FVoxelLandInfiniteWorldSnapshot> Source;
};
