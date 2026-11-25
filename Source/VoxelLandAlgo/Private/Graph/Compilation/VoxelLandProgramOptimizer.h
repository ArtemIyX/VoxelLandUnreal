#pragma once
#include "CoreMinimal.h"

struct FVoxelLandProgramDraft;

class FVoxelLandProgramOptimizer
{
public:
	static bool Optimize(FVoxelLandProgramDraft& Draft, FString& Error, TArray<FGuid>* InstructionNodes);
};