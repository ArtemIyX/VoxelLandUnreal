#include "World/Demo/VoxelLandClipmapDemo.h"

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"

#include "Components/SceneComponent.h"
#include "Misc/App.h"
#include "TimerManager.h"

AVoxelLandClipmapDemo::AVoxelLandClipmapDemo()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DemoRoot")));
	bEnableM7Clipmap = true;
}

void AVoxelLandClipmapDemo::BeginPlay()
{
	Super::BeginPlay();
	OnInitializationFinished.AddDynamic(this, &AVoxelLandClipmapDemo::HandleInitializationFinished);

	// The settings API requires BeginPlay to have finished.
	StartupTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AVoxelLandClipmapDemo::StartTerrain);
}

void AVoxelLandClipmapDemo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StartupTimer);
	OnInitializationFinished.RemoveDynamic(this, &AVoxelLandClipmapDemo::HandleInitializationFinished);
	Super::EndPlay(EndPlayReason);
}

void AVoxelLandClipmapDemo::StartTerrain()
{
	bDataOnly = bDataOnly || IsRunningDedicatedServer() || !FApp::CanEverRender();
	if (!IsValid(TerrainConfig.Get()))
	{
		LastInitializationError = TEXT("Assign a World Config asset on the demo actor.");
	}
	else if (TerrainConfig->IsDirectDensity() || !TerrainConfig->IsInfinite())
	{
		LastInitializationError = TEXT("Select Height Terrain and Infinite in World Config.");
	}
	else if (InitializeFromConfig(TerrainConfig.Get(), FIntVector::ZeroValue))
	{
		return;
	}
	HandleInitializationFinished(false);
}

void AVoxelLandClipmapDemo::HandleInitializationFinished(bool bSuccess)
{
	if (!bSuccess && !LastInitializationError.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *GetName(), *LastInitializationError);
	}
}
