#include "Generation/VoxelLandM5FieldInputs.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"

FVoxelLandM5FieldInputs::FVoxelLandM5FieldInputs(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Baseline(MoveTemp(InBaseline)), Influence(MoveTemp(InInfluence)), SourceBudget(MoveTemp(InBudget)), Lease(MoveTemp(InLease)) {}
FVoxelLandM5FieldInputs::~FVoxelLandM5FieldInputs() = default;
FVoxelLandM5Diagnostic FVoxelLandM5FieldInputs::Bind(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InBudget,
	TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5FieldInputs>& Out)
{
	FVoxelLandM5Diagnostic Error; Error.Stage = TEXT("M5.FieldInputs");
	if (IsCancelled()) { Error.Code = EVoxelLandM5ErrorCode::Cancelled; Error.Message = TEXT("Field input binding cancelled"); return Error; }
	if (&InBaseline->GetDefinition() != &InInfluence->GetDefinition() || !InBaseline->UsesBudget(*InBudget)
		|| !InInfluence->UsesBudget(*InBudget))
	{ Error.Code = EVoxelLandM5ErrorCode::InvalidInput; Error.Message = TEXT("Field owner mismatch"); return Error; }
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InputLease;
	if (!InBudget->TryChargeWork(1) || !InBudget->TryReserveBytes(sizeof(FVoxelLandM5FieldInputs) + 512, InputLease))
	{ Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded; Error.Message = TEXT("Field input admission exhausted"); return Error; }
	if (IsCancelled()) { Error.Code = EVoxelLandM5ErrorCode::Cancelled; Error.Message = TEXT("Field input binding cancelled"); return Error; }
	Out = MakeShareable(new FVoxelLandM5FieldInputs(InBaseline, InInfluence, InBudget, MoveTemp(InputLease)));
	return {};
}
