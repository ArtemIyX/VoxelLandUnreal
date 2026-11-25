#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5EvaluateChunk.h"
#include "Templates/Function.h"

struct FVoxelLandM5FinalFieldInputs;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5FieldInputs;
class FVoxelLandM5CandidateResult;

// Immutable scratch samples. This is not a certificate or publication token.
class VOXELLANDALGO_API FVoxelLandM5FinalField final
{
public:
	FVoxelLandM5FinalField(const FVoxelLandM5FinalField&) = delete;
	FVoxelLandM5FinalField& operator=(const FVoxelLandM5FinalField&) = delete;
	TConstArrayView<int16> GetDensity() const { return Density; }

	// All views remain immutable for the call. Output is assigned only on success.
	// Caller establishes baseline provenance, complete plan influence, conservative
	// masks, water, roof, entrance, route, attachment and enclosed-void safety.
	// Neither this result nor a successful build proves those obligations.
	// Enabled stages require composition version 2; enabled pockets additionally
	// require pocket-noise version 1 even when amplitude is zero.
	// Budget charges logical work and conservative array/sampler allowances, not
	// allocator overhead, borrowed inputs or upstream generator/provider work.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5FinalFieldInputs& Inputs,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField);

private:
	friend FVoxelLandM5Diagnostic VoxelLand::M5::FinalField::EvaluateChunk(const FVoxelLandM5FieldInputs&,
		const FVoxelLandChunkKey&, FVoxelLandWorkerContext&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>&);
	// Geometry 2 remains private to the exact sealed evaluation path.
	static FVoxelLandM5Diagnostic BuildOwned(const FVoxelLandM5FieldInputs& Owner,
		const FVoxelLandM5FinalFieldInputs& Inputs, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField);

	static FVoxelLandM5Diagnostic BuildInternal(const FVoxelLandM5FinalFieldInputs& Inputs,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, const FVoxelLandM5CandidateResult* Owned,
		const FVoxelLandM5Budget* SourceBudget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField);
	explicit FVoxelLandM5FinalField(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<int16> Density;
};
