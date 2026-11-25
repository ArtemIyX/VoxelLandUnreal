#pragma once

#include "CoreMinimal.h"
#include "Data/Program/Execution/VoxelLandProgramDraft.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ProgramDecoder;

// Immutable admitted decode result, not a validated or executable program.
// Destruction releases Draft storage before its budget lease.
class VOXELLANDALGO_API FVoxelLandM5DecodedDraft final
{
public:
	FVoxelLandM5DecodedDraft(const FVoxelLandM5DecodedDraft&) = delete;
	FVoxelLandM5DecodedDraft& operator=(const FVoxelLandM5DecodedDraft&) = delete;
	const FVoxelLandProgramDraft& GetDraft() const { return Draft; }
	uint64 GetExpectedProgramFingerprint() const { return ExpectedProgramFingerprint; }
	bool UsesBudget(const FVoxelLandM5Budget& Other) const { return &Budget.Get() == &Other; }

private:
	friend class FVoxelLandM5ProgramDecoder;
	FVoxelLandM5DecodedDraft(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InLease)
		: Budget(MoveTemp(InBudget))
		, Lease(MoveTemp(InLease))
	{
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandProgramDraft Draft;
	uint64 ExpectedProgramFingerprint = 0;
};
