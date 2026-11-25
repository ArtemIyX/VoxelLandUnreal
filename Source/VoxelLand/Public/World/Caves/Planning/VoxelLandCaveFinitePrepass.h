#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4WorldSnapshot;
class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
class FVoxelLandM4GridLease;
class FVoxelLandM4HeightGrid;
class FVoxelLandM5Budget;

namespace VoxelLand::M5::FinitePrepass
{
// Synchronous worker-thread scratch prepass. Snapshot and callback remain valid
// and immutable until return. The factory must synchronously create a worker for
// the exact supplied generator, on this thread; this function destroys every
// returned worker on the same thread before releasing its generator. Caller
// independently admits provider and worker resources, input storage and allocator
// overhead, and retains their admission through this call. The process lease is
// forwarded to the grid, but its provenance is not verified. This is neither
// provider admission nor world provenance or a cave certificate.
// Failure or cancellation preserves OutGrid and OutHash.
VOXELLAND_API FVoxelLandM5Diagnostic Generate(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash);
}
