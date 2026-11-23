#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
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
		Actors[1]->Initialize(nullptr);
		TestTrue(TEXT("null replacement preserves terrain state"), Actors[1]->HasWorldStateForTests());
		TestTrue(TEXT("null replacement preserves worker tick"), Actors[1]->IsActorTickEnabled());
		TestTrue(TEXT("null replacement reports failure"), Actors[1]->LastInitializationError.Contains(TEXT("missing")));
		Actors[1]->SetFailWorkerStartForTests(true);
		Actors[1]->Initialize(Generator);
		Actors[1]->SetFailWorkerStartForTests(false);
		TestTrue(TEXT("worker start failure preserves terrain state"), Actors[1]->HasWorldStateForTests());
		TestTrue(TEXT("worker start failure preserves tick"), Actors[1]->IsActorTickEnabled());
		TestTrue(TEXT("worker start failure reports error"),
			Actors[1]->LastInitializationError.Contains(TEXT("Unable to start terrain workers")));
		FVoxelLandChunkKey Startup;
		Startup.Coordinate = FIntVector(MAX_int32, 0, 0);
		TestFalse(TEXT("invalid startup rejected before replacement"),
			Actors[1]->TryInitializeWithStartupForTests(Generator, Startup));
		TestTrue(TEXT("invalid startup preserves active state"), Actors[1]->HasWorldStateForTests());
		Startup.Coordinate = FIntVector::ZeroValue;
		TestTrue(TEXT("replacement stages valid startup"),
			Actors[1]->TryInitializeWithStartupForTests(Generator, Startup));
		TestTrue(TEXT("startup demand is queued in replacement"), Actors[1]->IsExplicitChunkForTests(Startup));
		TestFalse(TEXT("missing settings rejected"), Actors[1]->InitializeFromSettings(nullptr));
		TestTrue(TEXT("missing settings preserve terrain state"), Actors[1]->HasWorldStateForTests());
		TestFalse(TEXT("missing M4 settings rejected"), Actors[1]->InitializeM4FromSettings(nullptr, FIntVector::ZeroValue));
		TestTrue(TEXT("missing M4 settings preserve terrain state"), Actors[1]->HasWorldStateForTests());
		UVoxelLandWorldSettings* Replacement = NewObject<UVoxelLandWorldSettings>();
		Replacement->Graph = NewObject<UVoxelLandTerrainGraph>(Replacement);
		Replacement->HorizontalRadius = 1;
		const int32 OriginalRadius = Actors[1]->HorizontalRadius;
		if (TestTrue(TEXT("replacement preparation starts"), Actors[1]->InitializeFromSettings(Replacement)))
		{
			TestTrue(TEXT("active terrain remains during preparation"), Actors[1]->HasWorldStateForTests());
			TestEqual(TEXT("active demand remains during preparation"), Actors[1]->HorizontalRadius, OriginalRadius);
			Actors[1]->CancelInitialization();
			TestTrue(TEXT("cancelled replacement preserves terrain"), Actors[1]->HasWorldStateForTests());
			TestTrue(TEXT("cancelled replacement preserves tick"), Actors[1]->IsActorTickEnabled());
			TestEqual(TEXT("cancelled replacement preserves demand"), Actors[1]->HorizontalRadius, OriginalRadius);
		}
		Actors[1]->SetShutdownHookForTests([&Actors, Generator] { Actors[1]->Initialize(Generator); });
		Actors[1]->Shutdown();
		TestTrue(TEXT("reentrant replacement survives retiring old state"), Actors[1]->HasWorldStateForTests());
		TestTrue(TEXT("reentrant replacement keeps ticking"), Actors[1]->IsActorTickEnabled());
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
