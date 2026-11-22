#pragma once

#include "CoreMinimal.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetState;

// One reservation. Retain a shared reference for as long as the corresponding
// allocation is live, including worker completion and all readers.
class VOXELLANDALGO_API FVoxelLandM5BudgetLease final
{
public:
	~FVoxelLandM5BudgetLease();
	FVoxelLandM5BudgetLease(const FVoxelLandM5BudgetLease&) = delete;
	FVoxelLandM5BudgetLease& operator=(const FVoxelLandM5BudgetLease&) = delete;

private:
	friend class FVoxelLandM5Budget;
	FVoxelLandM5BudgetLease(TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> InState, int64 InBytes);

	TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> State;
	const int64 Bytes;
};
