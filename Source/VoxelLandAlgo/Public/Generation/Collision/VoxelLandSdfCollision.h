#pragma once

#include "Data/Collision/VoxelLandCapsuleSweepRequest.h"
#include "Data/Collision/VoxelLandCapsuleSweepResult.h"

class FVoxelLandCollisionField;

class VOXELLANDALGO_API FVoxelLandSdfCollision final
{
public:
	[[nodiscard]] static FVoxelLandCapsuleSweepResult SweepCapsule(
		const FVoxelLandCollisionField& Field, const FVoxelLandCapsuleSweepRequest& Request);

	[[nodiscard]] static FVoxelLandCapsuleSweepResult OverlapCapsule(
		const FVoxelLandCollisionField& Field, const FVoxelLandCapsuleSweepRequest& Request);
};
