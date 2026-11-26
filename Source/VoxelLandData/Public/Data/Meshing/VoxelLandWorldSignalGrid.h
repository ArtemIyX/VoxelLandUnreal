#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"

struct FVoxelLandWorldSignalGrid
{
	FName Name;
	EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;
	FVector2d OriginCm = FVector2d::ZeroVector;
	double SpacingCm = 100.0;
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<double> Samples;
};
