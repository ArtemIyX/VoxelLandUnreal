#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandErrorCode.h"

struct FVoxelLandError
{
	EVoxelLandErrorCode Code = EVoxelLandErrorCode::None;
	FString Message;
	int32 Instruction = INDEX_NONE;
	FString Parameter;
	explicit operator bool() const { return Code != EVoxelLandErrorCode::None; }
};
