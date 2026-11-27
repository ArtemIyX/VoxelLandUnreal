#include "VoxelLandChaosViewport.h"

#if !UE_BUILD_SHIPPING && !UE_SERVER

#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/VoxelLandWorld.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "SceneInterface.h"
#include "SceneView.h"

TAutoConsoleVariable<int32> FVoxelLandChaosViewport::ShowCookWork(
	TEXT("voxel.Chaos.ShowCookWork"), 1, TEXT("Show pending VoxelLand Chaos work in development viewports."));

FVoxelLandChaosViewport::FVoxelLandChaosViewport(AVoxelLandWorld& InTerrain)
	: Terrain(&InTerrain)
{
	DrawHandle =
		UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FVoxelLandChaosViewport::Draw));
}

FVoxelLandChaosViewport::~FVoxelLandChaosViewport()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
	}
}

void FVoxelLandChaosViewport::Draw(UCanvas* const Canvas, APlayerController* const Controller) const
{
	const AVoxelLandWorld* const VoxelWorld = Terrain.Get();
	if (!Canvas || !VoxelWorld || ShowCookWork.GetValueOnGameThread() == 0)
	{
		return;
	}

	const FSceneView* const View = Canvas->SceneView;
	const FSceneInterface* const Scene = View && View->Family ? View->Family->Scene : nullptr;
	const UWorld* const DisplayedWorld = Controller ? Controller->GetWorld() : Scene ? Scene->GetWorld() : nullptr;
	if (DisplayedWorld != VoxelWorld->GetWorld())
	{
		return;
	}

	const FVoxelLandChaosStats Stats = VoxelWorld->GetChaosCollisionStats();
	const int32 Pending = Stats.WaitingForField + Stats.MeshQueued + Stats.MeshActive + Stats.CookQueued
						+ Stats.CookActive + Stats.ApplyQueued + Stats.DetachedCooks;
	if (Pending == 0)
	{
		return;
	}
	const FString Label = FString::Printf(
		TEXT(
			"VoxelLand Collision   Fields %d   Mesh %d queued / %d active   Cook %d queued / %d active   Apply %d   Detached %d"),
		Stats.WaitingForField, Stats.MeshQueued, Stats.MeshActive, Stats.CookQueued, Stats.CookActive,
		Stats.ApplyQueued, Stats.DetachedCooks);
	constexpr double ScreenMargin = 18.0;
	Canvas->K2_DrawText(UEngine::GetSmallFont(), Label, FVector2D(ScreenMargin, ScreenMargin));
}

#endif
