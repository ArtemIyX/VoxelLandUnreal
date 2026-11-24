#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/Function.h"

class FVoxelLandGenerator;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Immutable baseline scratch assembled from the same M4 generator. The lease
// remains alive until the final window reader releases its samples.
class VOXELLANDALGO_API FVoxelLandM5BaselineWindow final
{
public:
	FVoxelLandM5BaselineWindow(const FVoxelLandM5BaselineWindow&) = delete;
	FVoxelLandM5BaselineWindow& operator=(const FVoxelLandM5BaselineWindow&) = delete;
	FIntVector GetSize() const { return Size; }
	FVoxelLandM5PointCm GetOriginCm() const { return OriginCm; }
	int32 GetVoxelSizeCm() const { return VoxelSizeCm; }
	TConstArrayView<int16> GetDensity() const { return Density; }
	const FVoxelLandGenerator& GetGenerator() const { return *Generator; }
	bool IsOwnedBy(const FVoxelLandM5Budget& Budget) const;

	// Worker-thread only. Builds every LOD-zero chunk in [MinChunk,MaxChunk),
	// validates every M4 chunk footprint before allocation, verifies all duplicate
	// halo samples, and returns the merged X-fastest grid. Underlying M4 error
	// messages are retained in diagnostics, including finite-domain failures.
	// The caller retains provider/module and worker admission for M4 generation;
	// this budget covers output, seen flags, one returned chunk and assembly work,
	// not the generator's internal worker scratch or per-provider generation work.
	// The held generator binds the samples to that instance, NOT to a validated
	// world descriptor. This does not certify entrance/protection/water masks,
	// complete surface domains, final density, mesh safety, or publication.
	// MaxChunks is 1..512. Failure/cancellation leaves OutWindow unchanged.
	static FVoxelLandM5Diagnostic Build(TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator,
		const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& OutWindow);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5ScratchRoofFixtureFactory;
#endif
	FVoxelLandM5BaselineWindow(TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> InGenerator,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FIntVector Size = FIntVector::ZeroValue;
	FVoxelLandM5PointCm OriginCm;
	int32 VoxelSizeCm = 0;
	TArray<int16> Density;
};
