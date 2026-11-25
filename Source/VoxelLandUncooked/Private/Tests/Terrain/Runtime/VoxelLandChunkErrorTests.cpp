#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

namespace
{
constexpr EAutomationTestFlags M4ChunkErrorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

class FM4ChunkErrorCommand final : public IAutomationLatentCommand
{
public:
	FM4ChunkErrorCommand(FAutomationTestBase* InTest, UWorld* InWorld, AVoxelLandWorld* InActor)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Started(FPlatformTime::Seconds())
	{
	}

	bool Update() override
	{
		if (!IsValid(World) || !IsValid(Actor) || FPlatformTime::Seconds() - Started > 30.0)
		{
			Test->AddError(TEXT("M4 chunk error fixture timed out or lost its world"));
			Cleanup();
			return true;
		}
		Actor->Tick(0.016f);
		FString Error;
		if (!Actor->GetDataChunkError(FIntVector::ZeroValue, Error))
		{
			return false;
		}
		Test->TestTrue(TEXT("worker's slope error is exposed"), Error.Contains(TEXT("slope stencil")));
		Test->TestFalse(TEXT("failed chunk did not publish data"), Actor->HasDataChunk(FIntVector::ZeroValue));
		if (!bRetried)
		{
			bRetried = true;
			Test->TestTrue(TEXT("failed request can be retried"), Actor->RequestDataChunk(FIntVector::ZeroValue));
			Error = TEXT("old value");
			Test->TestFalse(
				TEXT("retry clears the prior error"), Actor->GetDataChunkError(FIntVector::ZeroValue, Error));
			Test->TestTrue(TEXT("no-error output is cleared"), Error.IsEmpty());
			return false;
		}
		Actor->ReleaseDataChunk(FIntVector::ZeroValue);
		Actor->Tick(0.016f);
		Test->TestFalse(
			TEXT("retired chunk no longer reports an error"), Actor->GetDataChunkError(FIntVector::ZeroValue, Error));
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
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}
	}
	FAutomationTestBase* Test;
	UWorld* World;
	AVoxelLandWorld* Actor;
	double Started;
	bool bRetried = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ChunkErrorTest, "VoxelLand.M4.World.ChunkError", M4ChunkErrorFlags)
bool FVoxelLandM4ChunkErrorTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("Engine unavailable"));
		return false;
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	const bool bCreated = Async(EAsyncExecution::ThreadPool, [&Generator] {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load, Store;
		Load.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			return false;
		}
		TArray<int32> Heights;
		for (int32 Y = 0; Y <= 80; ++Y)
		{
			for (int32 X = 0; X <= 80; ++X)
			{
				Heights.Add(X <= 40 ? -512000000 : 512000000);
			}
		}
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-40, -40, 80, 80, 1, Heights, Grid))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 1.f;
		return !FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Generator);
	}).Get();
	if (!TestTrue(TEXT("steep M4 height field created"), bCreated))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world created"), World))
	{
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
		AddError(TEXT("Could not spawn M4 world actor"));
		return false;
	}
	Actor->bDataOnly = true;
	Actor->Initialize(Generator);
	FString Error = TEXT("old value");
	TestFalse(TEXT("unrequested chunk has no error"), Actor->GetDataChunkError(FIntVector::ZeroValue, Error));
	TestTrue(TEXT("unrequested output is cleared"), Error.IsEmpty());
	AddExpectedError(
		TEXT("VoxelLand chunk (0,0,0): M4 slope stencil"), EAutomationExpectedErrorFlags::Contains, 2, false);
	if (!TestTrue(TEXT("supported footprint is accepted"), Actor->RequestDataChunk(FIntVector::ZeroValue)))
	{
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FM4ChunkErrorCommand(this, World, Actor));
	return true;
}
#endif
