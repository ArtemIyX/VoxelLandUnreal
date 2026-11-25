#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM7NormalFrame
{
	FVector3f Normal = FVector3f::UpVector;
	FVector3f Tangent = -FVector3f::RightVector;
	bool bFallback = false;
};
