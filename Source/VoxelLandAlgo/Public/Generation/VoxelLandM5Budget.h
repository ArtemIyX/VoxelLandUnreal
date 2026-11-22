#pragma once

#include "CoreMinimal.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5BudgetState;

// A native per-build accounting object. All operations and lease destruction
// are thread-safe; individual output variables still require caller ownership.
// Limits are immutable. This does not allocate or cancel the work being charged.
class VOXELLANDALGO_API FVoxelLandM5Budget final
{
public:
	// Reservation bookkeeping is bounded separately from payload bytes. The
	// actual allocator overhead is not included in the byte counter.
	static constexpr int32 MaximumLiveLeases = 1024;
	FVoxelLandM5Budget(const FVoxelLandM5Budget&) = delete;
	FVoxelLandM5Budget& operator=(const FVoxelLandM5Budget&) = delete;

	// Nonnegative limits, including zero, are allowed. OutBudget is unchanged
	// when validation fails. A budget is independent of all other budgets.
	static bool TryCreate(
		int64 MaxBytes, int64 MaxWork, TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& OutBudget);

	// Units must be nonnegative. Work charges accumulate for the budget lifetime
	// and are never returned, even if later work fails or is cancelled.
	bool TryChargeWork(int64 Units);

	// Bytes must be positive; zero-byte reservations are rejected. At most
	// MaximumLiveLeases reservations may exist concurrently per budget.
	// On failure the counters and OutLease are
	// unchanged. On success the reservation stays charged until the final
	// reference to the returned lease is released. Replacing OutLease may
	// release its old reservation after the new reservation has been admitted.
	bool TryReserveBytes(int64 Bytes, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& OutLease);

	// Returns the immutable configured maxima, not currently available capacity.
	void GetLimits(int64& MaxBytes, int64& MaxWork) const;

	// Returns one consistent snapshot. PeakBytes is the maximum admitted live
	// reservation, not a measurement of allocator overhead or uncharged memory.
	void GetUsage(int64& LiveBytes, int64& PeakBytes, int64& WorkUnits) const;

private:
	explicit FVoxelLandM5Budget(TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> InState);
	TSharedRef<FVoxelLandM5BudgetState, ESPMode::ThreadSafe> State;
};
