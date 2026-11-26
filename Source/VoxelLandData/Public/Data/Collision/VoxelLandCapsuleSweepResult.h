#pragma once

#include "CoreMinimal.h"
#include "Data/Collision/VoxelLandCapsuleSweepStatus.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"

struct FVoxelLandCapsuleSweepResult
{
	EVoxelLandCapsuleSweepStatus Status = EVoxelLandCapsuleSweepStatus::Invalid;
	// Contact Time is a witnessed upper bound within geometric roundoff tolerance.
	// LastSafeTime admits advance from a safe start. Zero keeps the original pose.
	double Time = 0.0;
	double LastSafeTime = 0.0;
	FVector3d Point = FVector3d::ZeroVector;
	FVector3d Normal = FVector3d::ZeroVector;
	TOptional<FVoxelLandChunkKey> MissingKey;
	int32 CellsVisited = 0;
	int32 RegionsVisited = 0;
};
