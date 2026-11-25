#pragma once

#include "CoreMinimal.h"

// Logical coordinates use signed 64-bit chunk and region indices. They are
// independent of renderer-origin rebasing and do not imply unbounded precision.
// All operations return false for invalid input or overflow, leaving every
// output unchanged. Multiple output references must refer to distinct objects.
namespace VoxelLand::InfiniteCoordinates
{
// Splits a chunk index into a mathematical-floor region and a nonnegative
// chunk offset in [0, ChunksPerRegion). ChunksPerRegion must be positive.
VOXELLANDALGO_API bool TrySplitChunk(int64 Chunk, int64 ChunksPerRegion, int64& OutRegion, int64& OutLocalChunk);

// Inverse of TrySplitChunk. Rejects noncanonical offsets and results outside
// the int64 chunk-index range, including at the extreme negative boundary.
VOXELLANDALGO_API bool TryJoinChunk(int64 Region, int64 LocalChunk, int64 ChunksPerRegion, int64& OutChunk);

// Computes a global sample relative to a logical chunk origin, in integer cm.
// The sample offset permits the one-sample negative and positive meshing halo:
// [-1, CellsPerChunk+1]. Output must fit [-MaxAbsLocalCm,+MaxAbsLocalCm].
// Chunk and OriginChunk are logical indices, not Unreal presentation positions.
VOXELLANDALGO_API bool TrySampleRelativeCm(int64 Chunk, int64 OriginChunk, int64 SampleOffset, int64 CellsPerChunk,
	int64 VoxelSizeCm, int64 MaxAbsLocalCm, int64& OutCm);

// Canonicalizes a face-adjacent pair of XY region keys in lexicographic order.
// Axis is 0 for an X face and 1 for a Y face. Inputs are not converted to
// absolute cm, so either region derives identical keys even at int64 edges.
VOXELLANDALGO_API bool TryCanonicalFace(int64 AX, int64 AY, int64 BX, int64 BY, int64& OutFirstX, int64& OutFirstY,
	int64& OutSecondX, int64& OutSecondY, uint8& OutAxis);
}
