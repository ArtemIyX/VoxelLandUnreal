#pragma once

#include "CoreMinimal.h"

class FVoxelLandCollisionField;
struct FVoxelLandWalkSettings;
struct FVoxelLandWalkState;

// Fixed-step standalone demonstration of the terrain queries.
class VOXELLAND_API FVoxelLandWalkMotor
{
public:
	// Positions are in logical centimeters. Unavailable or indeterminate queries preserve State.
	[[nodiscard]] static bool Step(const FVoxelLandCollisionField& Field, const FVoxelLandWalkSettings& Settings,
		const FVector2d& HorizontalInput, FVoxelLandWalkState& State);
};
