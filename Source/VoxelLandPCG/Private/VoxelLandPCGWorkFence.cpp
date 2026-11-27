#include "VoxelLandPCGWorkFence.h"

bool FVoxelLandPCGWorkFence::TryBeginWorker()
{
	uint32 Observed = State.Load();
	while ((Observed & CancelledBit) == 0 && (Observed & WorkerMask) != WorkerMask)
	{
		if (State.CompareExchange(Observed, Observed + 1u))
		{
			return true;
		}
	}
	return false;
}

void FVoxelLandPCGWorkFence::EndWorker()
{
	const uint32 Previous = State.DecrementExchange();
	check((Previous & WorkerMask) != 0);
}

void FVoxelLandPCGWorkFence::Cancel()
{
	State.OrExchange(CancelledBit);
}
