#pragma once

#include "CoreMinimal.h"
#include "Data/VoxelLandM5Handles.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5FieldInputs;
class FVoxelLandWorkerContext;
class FVoxelLandM5InfluenceSet;
struct FVoxelLandM5LatticePoint;

// Complete declared-range numerical comparison and mesher dependency closure.
// This does not prove roof, opening, protection, water, route or publication safety.
class VOXELLANDALGO_API FVoxelLandM5FieldChangeAudit final
{
public:
	~FVoxelLandM5FieldChangeAudit();
	FVoxelLandM5FieldChangeAudit(const FVoxelLandM5FieldChangeAudit&) = delete;
	FVoxelLandM5FieldChangeAudit& operator=(const FVoxelLandM5FieldChangeAudit&) = delete;

	[[nodiscard]] static FVoxelLandM5Diagnostic Build(TVoxelLandM5ConstRef<FVoxelLandM5FieldInputs> Inputs,
		FVoxelLandWorkerContext& Worker, FVoxelLandM5BudgetRef JobBudget, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM5FieldChangeAudit>& Out);

	// Counts are per stored sample, including repeated samples in neighboring chunks.
	int64 GetComparedSamples() const { return ComparedSamples; }
	int64 GetChangedSamples() const { return ChangedSamples; }
	int64 GetExaminedEdges() const { return ExaminedEdges; }
	const FVoxelLandM5FieldInputs& GetInputs() const { return Inputs.Get(); }

#if WITH_DEV_AUTOMATION_TESTS
	// Exercises dependency rejection only; cannot construct an audit result.
	[[nodiscard]] static FVoxelLandM5Diagnostic TestCheckSample(const FVoxelLandM5LatticePoint& Sample,
		const FVoxelLandM5InfluenceSet& Influence, int64 VoxelSizeCm, FVoxelLandM5BudgetRef Budget,
		TFunctionRef<bool()> IsCancelled);
#endif

private:
	FVoxelLandM5FieldChangeAudit(TVoxelLandM5ConstRef<FVoxelLandM5FieldInputs> InInputs,
		FVoxelLandM5BudgetRef InBudget, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);

	// Declared before owners so the admission survives their destruction.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TVoxelLandM5ConstRef<FVoxelLandM5FieldInputs> Inputs;
	FVoxelLandM5BudgetRef Budget;
	int64 ComparedSamples = 0;
	int64 ChangedSamples = 0;
	int64 ExaminedEdges = 0;
};
