#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Worlds/VoxelLandWorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "World/VoxelLandWorld.h"

namespace
{
constexpr uint64 CookedExampleHash = 11661623748142763676ull;

class FVoxelLandCookedRuntimeCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandCookedRuntimeCommand(FAutomationTestBase* InTest, UWorld* InWorld, AVoxelLandWorld* InActor,
		UVoxelLandWorldSettings* InSettings, FString InMountDirectory)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Settings(InSettings)
		, MountDirectory(MoveTemp(InMountDirectory))
		, StartTime(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		if (!IsValid(World) || !IsValid(Actor) || !IsValid(Settings))
		{
			Test->AddError(TEXT("Cooked runtime test objects became invalid"));
			Cleanup();
			return true;
		}
		if (FPlatformTime::Seconds() - StartTime > 30.0)
		{
			Test->AddError(TEXT("Cooked runtime initialization or chunk request timed out"));
			Cleanup();
			return true;
		}
		Actor->Tick(0.016f);
		if (Stage == 0)
		{
			if (Actor->bInitializing)
			{
				return false;
			}
			Test->TestFalse(TEXT("Changed settings reject the pending result"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("Changed settings report superseded result"),
				Actor->LastInitializationError.Contains(TEXT("superseded")));
			Settings->Seed = 1234;
			if (!Actor->InitializeFromSettings(Settings))
			{
				Test->AddError(FString::Printf(
					TEXT("Cooked settings initialization rejected: %s"), *Actor->LastInitializationError));
				Cleanup();
				return true;
			}
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (Actor->bInitializing)
			{
				return false;
			}
			if (!Actor->HasWorldStateForTests())
			{
				Test->AddError(FString::Printf(
					TEXT("Cooked settings initialization failed: %s"), *Actor->LastInitializationError));
				Cleanup();
				return true;
			}
			Key.Coordinate = FIntVector(0, 0, -1);
			if (!Actor->RequestChunk(Key))
			{
				Test->AddError(TEXT("Could not request data from cooked settings"));
				Cleanup();
				return true;
			}
			Stage = 2;
			return false;
		}
		const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = Actor->FindChunk(Key);
		if (!Chunk)
		{
			return false;
		}
		Test->TestEqual(TEXT("Cooked settings initialize the expected CPU field"), Chunk->Hash, CookedExampleHash);
		Test->AddInfo(FString::Printf(
			TEXT(
				"Cooked settings: no auto-start, cancellation, stale rejection, explicit initialization and chunk hash %llu"),
			static_cast<unsigned long long>(Chunk->Hash)));
		Cleanup();
		return true;
	}

private:
	void Cleanup()
	{
		if (IsValid(Actor))
		{
			Actor->Shutdown();
		}
		if (IsValid(World))
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
		if (IsValid(Settings))
		{
			Settings->RemoveFromRoot();
		}
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
	}
	FAutomationTestBase* Test;
	UWorld* World;
	AVoxelLandWorld* Actor;
	UVoxelLandWorldSettings* Settings;
	FString MountDirectory;
	double StartTime;
	int32 Stage = 0;
	FVoxelLandChunkKey Key;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCookedExampleProbe, "VoxelLand.Examples.CookedProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandCookedExampleProbe::RunTest(const FString& Parameters)
{
	FString Directory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VoxelLandCookedExampleDir="), Directory))
	{
		AddInfo(TEXT("Cooked probe skipped; supply -VoxelLandCookedExampleDir=<cooked Examples directory>."));
		return true;
	}
	FPaths::NormalizeDirectoryName(Directory);
	if (!FPaths::FileExists(Directory / TEXT("DA_M3_Hills.uasset"))
		|| !FPaths::FileExists(Directory / TEXT("DA_M3_WorldSettings.uasset")))
	{
		AddError(TEXT("Cooked example graph or settings not found in supplied directory."));
		return false;
	}
	const FString MountDirectory = FPaths::GetPath(Directory) + TEXT("/");
	FPackageName::RegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
	TGuardValue<int32> AllowCooked(GAllowCookedDataInEditorBuilds, 1);
	UVoxelLandWorldSettings* LoadedSettings = LoadObject<UVoxelLandWorldSettings>(
		nullptr, TEXT("/VoxelLand/Examples/DA_M3_WorldSettings.DA_M3_WorldSettings"));
	UVoxelLandTerrainGraph* Graph = LoadedSettings ? LoadedSettings->Graph.Get() : nullptr;
	if (!LoadedSettings || !Graph || !LoadedSettings->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly)
		|| !Graph->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		AddError(TEXT("Could not load cooked settings and their cooked graph reference."));
		return false;
	}
	TestNull(TEXT("Cook strips editor graph"), Graph->EditorGraph.Get());
	FString Error;
	if (!LoadedSettings->ValidateSettings(Error) || !GEngine)
	{
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		AddError(FString::Printf(TEXT("Cooked settings invalid: %s"), *Error));
		return false;
	}
	UVoxelLandWorldSettings* Settings = DuplicateObject<UVoxelLandWorldSettings>(LoadedSettings, GetTransientPackage());
	Settings->AddToRoot();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		Settings->RemoveFromRoot();
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		AddError(TEXT("Could not create transient runtime test world."));
		return false;
	}
	World->AddToRoot();
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!Actor)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Settings->RemoveFromRoot();
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		AddError(TEXT("Could not spawn transient terrain actor."));
		return false;
	}
	Actor->bDataOnly = true;
	Actor->DispatchBeginPlay();
	TestFalse(TEXT("BeginPlay does not start terrain"), Actor->HasWorldStateForTests());
	TestFalse(TEXT("BeginPlay leaves terrain tick disabled"), Actor->IsActorTickEnabled());
	if (!Actor->InitializeFromSettings(Settings))
	{
		AddError(FString::Printf(TEXT("Could not start cancellation probe: %s"), *Actor->LastInitializationError));
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Settings->RemoveFromRoot();
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		return false;
	}
	Actor->CancelInitialization();
	TestFalse(TEXT("Cancellation leaves no terrain state"), Actor->HasWorldStateForTests());
	TestFalse(TEXT("Cancellation ends initialization"), Actor->bInitializing);
	TestTrue(TEXT("Cancellation reports its reason"), Actor->LastInitializationError.Contains(TEXT("cancelled")));
	if (!Actor->InitializeFromSettings(Settings))
	{
		AddError(TEXT("Could not start stale-result probe"));
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Settings->RemoveFromRoot();
		FPackageName::UnRegisterMountPoint(TEXT("/VoxelLand/"), MountDirectory);
		return false;
	}
	++Settings->Seed;
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandCookedRuntimeCommand(this, World, Actor, Settings, MountDirectory));
	return true;
}
#endif
