#include "Generation/VoxelLandInfiniteCoordinates.h"

#include "Generation/VoxelLandM5CheckedMath.h"

namespace VoxelLand::InfiniteCoordinates
{
bool TrySplitChunk(int64 Chunk, int64 ChunksPerRegion, int64& OutRegion, int64& OutLocalChunk)
{
	if (ChunksPerRegion <= 0 || &OutRegion == &OutLocalChunk)
	{
		return false;
	}
	int64 Region = Chunk / ChunksPerRegion;
	int64 LocalChunk = Chunk % ChunksPerRegion;
	if (LocalChunk < 0)
	{
		// With a nonzero remainder the divisor is greater than one, so Region
		// cannot equal MIN_int64. LocalChunk + divisor is within [0, divisor).
		--Region;
		LocalChunk += ChunksPerRegion;
	}
	OutRegion = Region;
	OutLocalChunk = LocalChunk;
	return true;
}

bool TryJoinChunk(int64 Region, int64 LocalChunk, int64 ChunksPerRegion, int64& OutChunk)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (ChunksPerRegion <= 0 || LocalChunk < 0 || LocalChunk >= ChunksPerRegion)
	{
		return false;
	}
	int64 Chunk = 0;
	if (Region < 0)
	{
		// Region*size can be below MIN_int64 even when adding the local offset
		// brings the answer into range. Shift one region toward zero first.
		int64 NextRegionStart = 0;
		if (!TryMultiply(Region + 1, ChunksPerRegion, NextRegionStart)
			|| !TrySubtract(NextRegionStart, ChunksPerRegion - LocalChunk, Chunk))
		{
			return false;
		}
	}
	else if (!TryMultiply(Region, ChunksPerRegion, Chunk) || !TryAdd(Chunk, LocalChunk, Chunk))
	{
		return false;
	}
	OutChunk = Chunk;
	return true;
}

bool TrySampleRelativeCm(int64 Chunk, int64 OriginChunk, int64 SampleOffset, int64 CellsPerChunk, int64 VoxelSizeCm,
	int64 MaxAbsLocalCm, int64& OutCm)
{
	using namespace VoxelLand::M5::CheckedMath;
	int64 MaxSample = 0;
	if (CellsPerChunk <= 0 || VoxelSizeCm <= 0 || MaxAbsLocalCm < 0 || !TryAdd(CellsPerChunk, 1, MaxSample)
		|| SampleOffset < -1 || SampleOffset > MaxSample)
	{
		return false;
	}
	int64 ChunkDelta = 0;
	int64 Sample = 0;
	int64 LocalCm = 0;
	if (!TrySubtract(Chunk, OriginChunk, ChunkDelta) || !TryMultiply(ChunkDelta, CellsPerChunk, Sample)
		|| !TryAdd(Sample, SampleOffset, Sample) || !TryMultiply(Sample, VoxelSizeCm, LocalCm)
		|| LocalCm < -MaxAbsLocalCm || LocalCm > MaxAbsLocalCm)
	{
		return false;
	}
	OutCm = LocalCm;
	return true;
}

bool TryCanonicalFace(int64 AX, int64 AY, int64 BX, int64 BY, int64& OutFirstX, int64& OutFirstY, int64& OutSecondX,
	int64& OutSecondY, uint8& OutAxis)
{
	if (&OutFirstX == &OutFirstY || &OutFirstX == &OutSecondX || &OutFirstX == &OutSecondY || &OutFirstY == &OutSecondX
		|| &OutFirstY == &OutSecondY || &OutSecondX == &OutSecondY)
	{
		return false;
	}
	const bool bFirstBeforeSecond = AX < BX || (AX == BX && AY < BY);
	const int64 FirstX = bFirstBeforeSecond ? AX : BX;
	const int64 FirstY = bFirstBeforeSecond ? AY : BY;
	const int64 SecondX = bFirstBeforeSecond ? BX : AX;
	const int64 SecondY = bFirstBeforeSecond ? BY : AY;
	uint8 Axis = 0;
	if (FirstY == SecondY && FirstX < MAX_int64 && FirstX + 1 == SecondX)
	{
		Axis = 0;
	}
	else if (FirstX == SecondX && FirstY < MAX_int64 && FirstY + 1 == SecondY)
	{
		Axis = 1;
	}
	else
	{
		return false;
	}
	OutFirstX = FirstX;
	OutFirstY = FirstY;
	OutSecondX = SecondX;
	OutSecondY = SecondY;
	OutAxis = Axis;
	return true;
}
}
