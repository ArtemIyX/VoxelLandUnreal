#include "World/Collision/Chaos/VoxelLandChaosTerrainComponentHost.h"

#include "Components/SceneComponent.h"

AVoxelLandChaosTerrainComponentHost::AVoxelLandChaosTerrainComponentHost()
{
	USceneComponent* CollisionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CollisionRoot"));
	CollisionRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(CollisionRoot);

	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SetCanBeDamaged(false);
	SetActorEnableCollision(true);
}
