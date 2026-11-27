#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChaosMesh
{
	TArray<FVector3f> Vertices;
	TArray<uint32> Indices;
	FBox3f Bounds = FBox3f(ForceInit);
	uint8 EffectiveLod = 0;
};
