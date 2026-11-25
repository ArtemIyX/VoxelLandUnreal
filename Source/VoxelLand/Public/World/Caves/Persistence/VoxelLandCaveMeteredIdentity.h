#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Persistence/VoxelLandCaveIdentityRecord.h"
#include "Templates/Function.h"

struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ProtectionGrid;

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

	// Descriptor 3 only: encodes every immutable mask byte and its geometry.
	// The grid must be retained throughout and charged to this same budget.
	// This does not establish source freshness or authorize publication.
	static FVoxelLandM5Diagnostic Capture(const FVoxelLandM5WorldSnapshot& Snapshot,
		const FVoxelLandM5ProtectionGrid& ProtectionGrid, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe>& OutIdentity);

private:
	static FVoxelLandM5Diagnostic CaptureInternal(const FVoxelLandM5WorldSnapshot& Snapshot,
		const FVoxelLandM5ProtectionGrid* ProtectionGrid, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe>& OutIdentity);
	explicit FVoxelLandM5MeteredIdentity(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandM5IdentityRecord Identity;
};
