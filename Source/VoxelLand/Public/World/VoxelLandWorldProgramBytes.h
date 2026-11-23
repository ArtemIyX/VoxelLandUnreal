#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
struct FVoxelLandSerializedProgram;

// Immutable canonical bytes for one program envelope. The record owns its
// reservation; it does not prove source identity, provider availability or validity.
class VOXELLAND_API FVoxelLandWorldProgramBytes final
{
public:
	FVoxelLandWorldProgramBytes(const FVoxelLandWorldProgramBytes&) = delete;
	FVoxelLandWorldProgramBytes& operator=(const FVoxelLandWorldProgramBytes&) = delete;

	// Caller retains stable input. Counts, ASCII names and logical byte/work
	// costs are bounded before allocation. Failure leaves OutBytes unchanged.
	static FVoxelLandM5Diagnostic Encode(const FVoxelLandSerializedProgram& Program,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandWorldProgramBytes, ESPMode::ThreadSafe>& OutBytes);
	const TArray<uint8>& GetBytes() const { return Bytes; }

private:
	FVoxelLandWorldProgramBytes(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease, TArray<uint8>&& InBytes);
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint8> Bytes;
};
