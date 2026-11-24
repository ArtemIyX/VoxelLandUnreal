#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4WorldSnapshot;
class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
class FVoxelLandM4GridLease;
class FVoxelLandM4HeightGrid;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Scratch generator and its grid; not a complete identity or freshness proof,
// provider admission, cave certificate, or permission to publish a world.
class VOXELLAND_API FVoxelLandM5FiniteBaseScratch final
{
public:
	// Synchronous worker thread only. Snapshot and callback remain immutable
	// until return. Caller separately admits and retains worker/provider resources,
	// process lease, input storage and allocator overhead. Workers must be created
	// for the supplied generator on this thread. Failure preserves Out.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM4WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease,
		TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
			TUniquePtr<FVoxelLandWorkerContext>&)> CreateWorker,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe>& Out);
	const FVoxelLandGenerator& GetGenerator() const { return *Generator; }
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> GetGeneratorShared() const { return Generator.ToSharedRef(); }
	const FVoxelLandM4HeightGrid& GetGrid() const { return *Grid; }
	uint64 GetHash() const { return Hash; }
private:
	explicit FVoxelLandM5FiniteBaseScratch(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
		: Lease(MoveTemp(InLease)) {}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	uint64 Hash = 0;
};
