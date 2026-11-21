#pragma once

#include "CoreMinimal.h"

// A named global output and its producer. Name comparison is case-sensitive.
struct FVoxelLandM4StageOutput
{
	FString Name;
	FGuid Producer;
};
