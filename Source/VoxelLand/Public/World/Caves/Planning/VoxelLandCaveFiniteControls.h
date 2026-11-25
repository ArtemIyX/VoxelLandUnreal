#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4WorldSnapshot;
struct FVoxelLandM4IdentityRecord;
class FVoxelLandGenerator;
class FVoxelLandM4CurveLut;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5MeteredM4Identity;

// Bounded finite control setup only. It is not a prepass or a cave certificate.
class VOXELLAND_API FVoxelLandM5FiniteControls final
{
public:
	FVoxelLandM5FiniteControls(const FVoxelLandM5FiniteControls&) = delete;
	FVoxelLandM5FiniteControls& operator=(const FVoxelLandM5FiniteControls&) = delete;

	// Worker thread only. Snapshot remains immutable throughout the call. This
	// retains identity, program and generator admissions plus a logical allowance
	// for curves and setup; native worker/provider allocations, input storage and
	// allocator bookkeeping remain separate obligations. Failure or cancellation
	// preserves OutControls. Returned references require this object to stay alive.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM4WorldSnapshot& Snapshot,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5FiniteControls, ESPMode::ThreadSafe>& OutControls);

	// Indices 0, 1 and 2 are continental, mountain and peaks respectively.
	const FVoxelLandGenerator& GetGenerator(int32 Index) const;
	const FVoxelLandM4CurveLut& GetCurve(int32 Index) const;
	const FVoxelLandM4IdentityRecord& GetIdentity() const;

private:
	explicit FVoxelLandM5FiniteControls(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	// Released after all retained data and curve allocations.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Identity;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generators[3];
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curves[3];
};
