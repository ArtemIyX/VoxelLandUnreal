#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5IdentityRecord.h"
#include "Templates/Function.h"

struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Pre-release finite M5 descriptor identity only: not a cave plan, water
// certificate or authorization to publish. Ignore water policy is not dry proof.
class VOXELLAND_API FVoxelLandM5MeteredIdentity final
{
public:
	FVoxelLandM5MeteredIdentity(const FVoxelLandM5MeteredIdentity&) = delete;
	FVoxelLandM5MeteredIdentity& operator=(const FVoxelLandM5MeteredIdentity&) = delete;
	const FVoxelLandM5IdentityRecord& GetIdentity() const { return Identity; }

	// Worker thread only; input must remain immutable throughout. Finite M4
	// inputs are checked by the metered built-in path, which can return Unproven.
	// Logical work and memory are charged; caller owns provider/module lifetime
	// and allocator overhead. Failure or cancellation preserves OutIdentity.
	static FVoxelLandM5Diagnostic Capture(const FVoxelLandM5WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe>& OutIdentity);

private:
	explicit FVoxelLandM5MeteredIdentity(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandM5IdentityRecord Identity;
};
