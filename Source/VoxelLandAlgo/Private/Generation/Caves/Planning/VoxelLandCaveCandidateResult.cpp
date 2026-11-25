#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"

#include "Generation/Caves/Persistence/VoxelLandCavePlanEncoding.h"

FVoxelLandM5Diagnostic FVoxelLandM5CandidateResult::BindCanonicalPlan(TFunctionRef<bool()> IsCancelled)
{
	if (!CanonicalPlanBytes.IsEmpty() || CanonicalPlanLease.IsValid())
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::InvalidInput;
		Error.Stage = TEXT("M5.PlanEncoding");
		Error.Message = TEXT("Plan identity is already bound");
		return Error;
	}
	return VoxelLand::M5::PlanEncoding::Encode(Plan, Budget, IsCancelled, CanonicalPlanBytes, CanonicalPlanLease);
}
