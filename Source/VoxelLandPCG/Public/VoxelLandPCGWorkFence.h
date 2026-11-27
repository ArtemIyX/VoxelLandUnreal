#pragma once

#include "CoreMinimal.h"

// One state word serializes cancellation and admission. A retired terrain
// snapshot remains charged to the scheduler while any worker owns its data.
class VOXELLANDPCG_API FVoxelLandPCGWorkFence final
{
public:
	[[nodiscard]] bool TryBeginWorker();
	void EndWorker();
	void Cancel();
	[[nodiscard]] bool IsCancelled() const { return (State.Load() & CancelledBit) != 0; }
	[[nodiscard]] bool HasWorkers() const { return GetWorkerCount() != 0; }
	[[nodiscard]] int32 GetWorkerCount() const { return static_cast<int32>(State.Load() & WorkerMask); }

private:
	static constexpr uint32 CancelledBit = 1u << 31;
	static constexpr uint32 WorkerMask = CancelledBit - 1u;
	TAtomic<uint32> State{0u};
};
