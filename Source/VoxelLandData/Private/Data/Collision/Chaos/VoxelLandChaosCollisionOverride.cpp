#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"

#include "Data/Collision/Chaos/VoxelLandChaosCollisionSettings.h"

bool FVoxelLandChaosCollisionOverride::IsValid() const
{
	switch (Mode)
	{
		case EVoxelLandChaosCollisionOverrideMode::Inherit:
		case EVoxelLandChaosCollisionOverrideMode::Disabled:
			return true;
		case EVoxelLandChaosCollisionOverrideMode::Enabled:
			return !bOverrideDetail || FVoxelLandChaosCollisionSettings::GetLod(Detail) != INDEX_NONE;
		default:
			return false;
	}
}
