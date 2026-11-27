#pragma once

#include "GameFramework/Actor.h"
#include "VoxelLandChaosTerrainComponentHost.generated.h"

// Local-only static attachment root. Unreal rebases its collision children as a unit.
UCLASS(Transient, NotBlueprintable)
class VOXELLAND_API AVoxelLandChaosTerrainComponentHost final : public AActor
{
	GENERATED_BODY()

public:
	AVoxelLandChaosTerrainComponentHost();
};
