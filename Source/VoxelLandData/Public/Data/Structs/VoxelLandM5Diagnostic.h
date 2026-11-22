#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5ErrorCode.h"

// Diagnostics describe failures, never serve as proof that a plan is safe.
struct FVoxelLandM5Diagnostic
{
	EVoxelLandM5ErrorCode Code = EVoxelLandM5ErrorCode::None;
	FName Stage;
	bool bHasFeatureId = false;
	uint64 FeatureId = 0;
	FString Message;
};
