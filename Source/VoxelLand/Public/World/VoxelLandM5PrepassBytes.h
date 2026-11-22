#pragma once

#include "CoreMinimal.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5MeteredPrepass;

// Owns the admitted canonical prepass bytes until the final reader releases it.
class VOXELLAND_API FVoxelLandM5PrepassBytes final
{
public:
	FVoxelLandM5PrepassBytes(const FVoxelLandM5PrepassBytes&) = delete;
	FVoxelLandM5PrepassBytes& operator=(const FVoxelLandM5PrepassBytes&) = delete;
	const TArray<uint8>& GetBytes() const { return Bytes; }
	bool UsesBudget(const FVoxelLandM5Budget* Other) const { return &Budget.Get() == Other; }

private:
	friend class FVoxelLandM5MeteredPrepass;
	FVoxelLandM5PrepassBytes(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InLease)
		: Budget(InBudget)
		, Lease(MoveTemp(InLease))
	{
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint8> Bytes;
};
