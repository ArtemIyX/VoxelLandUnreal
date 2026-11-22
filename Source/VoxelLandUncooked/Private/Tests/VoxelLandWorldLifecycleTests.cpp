#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldLifecycleIsolationTest, "VoxelLand.World.Lifecycle.Isolation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldLifecycleIsolationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("engine available"), GEngine))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	const bool bMadeGenerator = Async(EAsyncExecution::ThreadPool, [&Generator] {
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadZ;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		return !FVoxelLandGenerator::Create(Settings, Generator);
	}).Get();
	if (!TestTrue(TEXT("worker generated immutable terrain program"), bMadeGenerator))
	{
		return false;
	}
	UWorld* Worlds[2] = {UWorld::CreateWorld(EWorldType::Game, false), UWorld::CreateWorld(EWorldType::Game, false)};
	AVoxelLandWorld* Actors[2] = {};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!Worlds[Index])
		{
			AddError(TEXT("Could not create lifecycle world"));
			continue;
		}
		Worlds[Index]->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Worlds[Index]);
		Worlds[Index]->InitializeActorsForPlay(FURL());
		Actors[Index] = Worlds[Index]->SpawnActor<AVoxelLandWorld>();
		if (TestNotNull(TEXT("terrain actor created"), Actors[Index]))
		{
			Actors[Index]->bDataOnly = true;
			Actors[Index]->DispatchBeginPlay();
		}
	}
	if (Actors[0] && Actors[1])
	{
		// The first actor owns a pending demo and the second has independent
		// active workers, as separate PIE worlds do in one editor process.
		Actors[0]->StartDemo();
		Actors[1]->Initialize(Generator);
		TestTrue(TEXT("first world started its asynchronous demo"), Actors[0]->IsActorTickEnabled());
		TestTrue(TEXT("second world owns active terrain state"), Actors[1]->HasWorldStateForTests());
		Actors[0]->EndPlay(EEndPlayReason::EndPlayInEditor);
		TestFalse(TEXT("first actor stops its own job"), Actors[0]->IsActorTickEnabled());
		TestTrue(TEXT("other world retains terrain state"), Actors[1]->HasWorldStateForTests());
		TestTrue(TEXT("other world remains enabled"), Actors[1]->IsActorTickEnabled());
		TestTrue(TEXT("other world accepts chunk demand"), Actors[1]->RequestDataChunk(FIntVector::ZeroValue));
	}
	for (UWorld* World : Worlds)
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	}
	return true;
}

#endif
