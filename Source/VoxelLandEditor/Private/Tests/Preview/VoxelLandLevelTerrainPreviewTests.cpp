#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "World/VoxelLandWorld.h"
#include "Worlds/VoxelLandTerrainPreview.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandLevelTerrainPreviewCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandLevelTerrainPreviewCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandTerrainPreview& InPreview)
		: Test(InTest)
		, World(&InWorld)
		, Preview(&InPreview)
	{
	}

	bool Update() override
	{
		if (!Preview.IsValid() || FPlatformTime::Seconds() > Deadline)
		{
			Test.AddError(Preview.IsValid() ? Preview->PreviewStatus.ToString() : TEXT("Preview actor was lost."));
			return Finish();
		}
		World->Tick(LEVELTICK_ViewportsOnly, 1.0f / 60.0f);
		AVoxelLandWorld* Current = Preview->GetPreviewWorld();
		if (!Current)
		{
			return false;
		}
		if (!FirstWorld.IsValid() && !bStartedReplacement)
		{
			return BeginReplacement(*Current);
		}
		if (Current == FirstWorld.Get())
		{
			Test.TestTrue(
				TEXT("The previous terrain stays visible while the replacement runs"), HasVisibleRegion(*Current));
			return false;
		}
		Test.TestTrue(TEXT("Replacement world published its terrain"), HasVisibleRegion(*Current));
		Test.TestFalse(TEXT("Superseded terrain actor was retired"), FirstWorld.IsValid());
		UVoxelLandWorldConfig* Config = Preview->WorldConfig.Get();
		Preview->WorldConfig = nullptr;
		Preview->GeneratePreview();
		Test.TestTrue(TEXT("Invalid config leaves the last successful preview visible"),
			Preview->GetPreviewWorld() == Current && HasVisibleRegion(*Current));
		Preview->WorldConfig = Config;
		Preview->GeneratePreview();
		Preview->ClearPreview();
		Test.TestNull(TEXT("Explicit clear removes the published source"), Preview->GetPreviewWorld());
		const TActorIterator<AVoxelLandWorld> RemainingTerrain(World.Get());
		const TActorIterator<AVoxelLandRegion> RemainingRegion(World.Get());
		Test.TestFalse(TEXT("No transient terrain actor survives clear"), static_cast<bool>(RemainingTerrain));
		Test.TestFalse(TEXT("No transient terrain region survives clear"), static_cast<bool>(RemainingRegion));
		return Finish();
	}

private:
	bool BeginReplacement(AVoxelLandWorld& Current)
	{
		Test.TestFalse(TEXT("Level preview terrain initializes without BeginPlay"), Current.HasActorBegunPlay());
		Test.TestTrue(TEXT("Generated world cannot be saved or duplicated into PIE"),
			Current.HasAllFlags(RF_Transient | RF_DuplicateTransient));
		Test.TestTrue(TEXT("Terrain has a registered region visible in the level editor"), HasVisibleRegion(Current));
		for (TActorIterator<AVoxelLandRegion> Region(World.Get()); Region; ++Region)
		{
			if (Region->GetOwner() == &Current)
			{
				Test.TestTrue(TEXT("Generated region cannot be saved or duplicated into PIE"),
					Region->HasAllFlags(RF_Transient | RF_DuplicateTransient));
			}
		}
		FirstWorld = &Current;
		bStartedReplacement = true;
		Preview->PreviewExtentCm = FVector(1800.0, 1800.0, 4800.0);
		Preview->GeneratePreview();
		Test.TestTrue(TEXT("Generating a replacement keeps the prior source"),
			Preview->GetPreviewWorld() == &Current && HasVisibleRegion(Current));
		return false;
	}

	bool HasVisibleRegion(const AVoxelLandWorld& Terrain) const
	{
		for (TActorIterator<AVoxelLandRegion> Region(World.Get()); Region; ++Region)
		{
			if (Region->GetOwner() == &Terrain && !Region->IsTemporarilyHiddenInEditor())
			{
				return true;
			}
		}
		return false;
	}

	bool Finish()
	{
		if (Preview.IsValid())
		{
			Preview->Destroy();
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World.Get());
		World.Reset();
		return true;
	}

	FAutomationTestBase& Test;
	TStrongObjectPtr<UWorld> World;
	TWeakObjectPtr<AVoxelLandTerrainPreview> Preview;
	TWeakObjectPtr<AVoxelLandWorld> FirstWorld;
	double Deadline = FPlatformTime::Seconds() + 30.0;
	bool bStartedReplacement = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandLevelTerrainPreviewTest, "VoxelLand.PCG.Editor.FiniteTerrainPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandLevelTerrainPreviewTest::RunTest(const FString& Parameters)
{
	const UVoxelLandWorldConfig* Shipped =
		LoadObject<UVoxelLandWorldConfig>(nullptr, TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"));
	if (!TestNotNull(TEXT("Finite reference World Config"), Shipped)
		|| !TestNotNull(TEXT("Editor engine"), GEngine))
	{
		return false;
	}
	UWorld::InitializationValues Init;
	Init.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(
		EWorldType::Editor, false, NAME_None, GetTransientPackage(), false, ERHIFeatureLevel::Num, &Init));
	if (!TestNotNull(TEXT("Isolated level editor world"), World.Get()))
	{
		return false;
	}

	GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World.Get());
	AVoxelLandTerrainPreview* Preview = World->SpawnActor<AVoxelLandTerrainPreview>();
	if (!TestNotNull(TEXT("Placeable preview actor"), Preview))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World.Get());
		return false;
	}
	TestTrue(TEXT("Preview is an editor-only authoring actor"), Preview->IsEditorOnly());
	Preview->WorldConfig = DuplicateObject<UVoxelLandWorldConfig>(Shipped, Preview);
	Preview->WorldConfig->bGlobalErosion = false;
	Preview->WorldConfig->CellsX = 16;
	Preview->WorldConfig->CellsY = 16;
	Preview->WorldConfig->GridSpacingCm = 1600;
	Preview->WorldConfig->OriginXCm = -12800;
	Preview->WorldConfig->OriginYCm = -12800;
	Preview->PreviewExtentCm = FVector(1200.0, 1200.0, 4800.0);
	Preview->GeneratePreview();
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandLevelTerrainPreviewCommand(*this, *World, *Preview));
	return true;
}

#endif
