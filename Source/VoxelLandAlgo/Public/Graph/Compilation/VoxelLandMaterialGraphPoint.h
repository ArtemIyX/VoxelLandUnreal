#pragma once

#include "CoreMinimal.h"

class IVoxelLandMaterialSignalSource;

struct FVoxelLandMaterialGraphPoint
{
	FVector3d Position = FVector3d::ZeroVector;
	double SlopeDegrees = 0.0;
	uint64 Seed = 0;
	const IVoxelLandMaterialSignalSource* Signals = nullptr;
};
