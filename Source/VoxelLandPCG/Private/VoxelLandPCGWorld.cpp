#include "VoxelLandPCGWorld.h"

#include "VoxelLandPCGComponent.h"
#include "Components/SceneComponent.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "TimerManager.h"

AVoxelLandPCGWorld::AVoxelLandPCGWorld()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VoxelWorldRoot")));
	Placement = CreateDefaultSubobject<UVoxelLandPCGComponent>(TEXT("Placement"));
	bEnableM7Clipmap = true;
}

void AVoxelLandPCGWorld::BeginPlay()
{
	Super::BeginPlay();
	Placement->SetSourceWorld(this);
	Placement->SetWorldConfig(Config);
	StartupTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AVoxelLandPCGWorld::StartTerrain);
}

void AVoxelLandPCGWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StartupTimer);
	Placement->ClearPreview();
	Super::EndPlay(EndPlayReason);
}

void AVoxelLandPCGWorld::StartTerrain()
{
	if (!Config)
	{
		return;
	}
	bEnableM7Clipmap = bEnableM7Clipmap && !Config->IsDirectDensity();
	if (InitializeFromConfig(Config, StartupChunk))
	{
		Placement->RefreshPlacement();
	}
}
