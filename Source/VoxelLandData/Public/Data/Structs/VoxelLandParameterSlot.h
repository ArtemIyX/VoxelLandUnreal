#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandParameterType.h"

struct FVoxelLandParameterSlot
{
	FString Name; // ASCII identifier, matched case-insensitively.
	EVoxelLandParameterType Type = EVoxelLandParameterType::Float;
	float FloatDefault = 0.0f;
	int32 IntDefault = 0;
};
