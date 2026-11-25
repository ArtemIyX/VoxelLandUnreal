#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4WorldSnapshot;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Validated finite M4 identity bytes, not an M5 identity or a cave certificate.
// Provider/module lifetime and allocator overhead remain caller obligations.
class VOXELLAND_API FVoxelLandM5MeteredM4Identity final
{
public:
	FVoxelLandM5MeteredM4Identity(const FVoxelLandM5MeteredM4Identity&) = delete;
	FVoxelLandM5MeteredM4Identity& operator=(const FVoxelLandM5MeteredM4Identity&) = delete;
	const FVoxelLandM4IdentityRecord& GetIdentity() const { return Identity; }

	// Worker thread only. Snapshot remains immutable throughout. Supports only
	// finite inputs accepted by the metered prepass and built-in program loader.
	// Logical work and memory are charged before the bounded serializer runs.
	// Cancellation or failure preserves OutIdentity.
	static FVoxelLandM5Diagnostic Capture(const FVoxelLandM4WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe>& OutIdentity);

private:
	explicit FVoxelLandM5MeteredM4Identity(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandM4IdentityRecord Identity;
};
