#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4WorldDemandTest, "VoxelLand.M4.World.DemandBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4WorldDemandTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> M4Generator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> M3Generator;
	const bool bGenerated = Async(EAsyncExecution::ThreadPool, [&]() {
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
		if (FVoxelLandGenerator::Create(Settings, M3Generator))
		{
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(0, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			return false;
		}
		return !FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, M4Generator);
	}).Get();
	if (!TestTrue(TEXT("M3 and M4 generators created off thread"), bGenerated) || !GEngine)
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world created"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("terrain actor created"), Actor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Actor->HorizontalRadius = 1;
	Actor->VerticalRadius = 1;
	Actor->MaxResidentChunks = 100;
	Actor->Initialize(M4Generator);
	Actor->SetTestViewLocation(FVector::ZeroVector);
	TestTrue(TEXT("explicit in-domain chunk accepted"), Actor->RequestDataChunk(FIntVector(0, 0, 0)));
	TestFalse(TEXT("explicit out-of-domain chunk rejected"), Actor->RequestDataChunk(FIntVector(1, 0, 0)));
	Actor->Tick(0.016f);
	TestTrue(TEXT("camera requests in-domain chunk"), Actor->IsChunkDemanded(FIntVector(0, 0, 0)));
	TestFalse(TEXT("camera excludes outside chunk"), Actor->IsChunkDemanded(FIntVector(1, 0, 0)));
	TestFalse(TEXT("camera excludes negative outside chunk"), Actor->IsChunkDemanded(FIntVector(-2, 0, 0)));
	TestEqual(TEXT("camera selects only supported XY columns"), Actor->GetDemandedChunkCount(), 20);
	Actor->Initialize(M3Generator);
	TestTrue(TEXT("M3 keeps its existing unrestricted demand"), Actor->RequestDataChunk(FIntVector(1, 0, 0)));
	Actor->Shutdown();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}
#endif
