#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4WorldDemandTest, "VoxelLand.M4.World.DemandBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4WorldDemandTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> M4Generator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> M3Generator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> WiderM4Generator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> UnlimitedGenerator;
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
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, M4Generator))
		{
			return false;
		}
		Heights.Init(0, 201 * 201);
		if (!FVoxelLandM4HeightGrid::Create(-10000, -10000, 200, 200, 100, Heights, Grid))
		{
			return false;
		}
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, WiderM4Generator))
		{
			return false;
		}
		FVoxelLandProgramDraft ControlDraft;
		ControlDraft.FormatVersion = 2;
		ControlDraft.RegisterCount = 1;
		ControlDraft.Constants = {0.f};
		FVoxelLandInstruction Constant;
		Constant.Op = EVoxelLandOpcode::LoadConstant;
		FVoxelLandInstruction Output;
		Output.Op = EVoxelLandOpcode::StoreDensity;
		ControlDraft.Instructions = {Constant, Output};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> ControlProgram;
		if (FVoxelLandProgram::Validate(ControlDraft, ControlProgram))
		{
			return false;
		}
		Settings.Program = ControlProgram;
		Settings.Profile.VoxelSizeCm = 1.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Control;
		if (FVoxelLandGenerator::Create(Settings, Control))
		{
			return false;
		}
		TArray<int32> Samples;
		Samples.Init(0, FVoxelLandM4CurveLut::SampleCount);
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		FString Error;
		if (!FVoxelLandM4CurveLut::Create(Samples, -100, 100, Curve, Error))
		{
			return false;
		}
		FVoxelLandM4UnlimitedConfig Local;
		Local.Backend = EVoxelLandM4UnlimitedBackend::FixedLattice;
		Local.ContinentalGenerator = Local.MountainGenerator = Local.PeaksGenerator = Control;
		Local.ContinentalCurve = Local.MountainCurve = Local.PeaksCurve = Curve;
		return !FVoxelLandGenerator::CreateM4Unlimited(Settings, Local, UnlimitedGenerator);
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
	TestTrue(TEXT("out-of-domain camera demand is reported"), Actor->M4RejectedCameraDemandCount > 0);
	TestTrue(TEXT("boundary rejection includes an actionable reason"),
		Actor->M4FirstCameraDemandRejection.Contains(TEXT("outside the height grid")));
	Actor->Initialize(WiderM4Generator);
	TestEqual(TEXT("reinitialization clears old boundary count"), Actor->M4RejectedCameraDemandCount, 0);
	TestTrue(TEXT("reinitialization clears old boundary reason"), Actor->M4FirstCameraDemandRejection.IsEmpty());
	Actor->SetTestViewLocation(FVector::ZeroVector);
	Actor->Tick(0.016f);
	TestEqual(TEXT("interior camera has no rejected chunks"), Actor->M4RejectedCameraDemandCount, 0);
	Actor->SetTestViewLocation(FVector(9600, 0, 0));
	Actor->Tick(0.016f);
	TestTrue(TEXT("moving to the edge reports rejected chunks"), Actor->M4RejectedCameraDemandCount > 0);
	TestFalse(TEXT("edge has a rejection reason"), Actor->M4FirstCameraDemandRejection.IsEmpty());
	Actor->SetTestViewLocation(FVector::ZeroVector);
	Actor->Tick(0.016f);
	TestEqual(TEXT("moving inward clears boundary count"), Actor->M4RejectedCameraDemandCount, 0);
	TestTrue(TEXT("moving inward clears boundary reason"), Actor->M4FirstCameraDemandRejection.IsEmpty());
	Actor->Initialize(M3Generator);
	TestTrue(TEXT("M3 keeps its existing unrestricted demand"), Actor->RequestDataChunk(FIntVector(1, 0, 0)));
	TestFalse(TEXT("M3 still rejects extended mesh envelope"), Actor->RequestDataChunk(FIntVector(1000000, 0, 0)));
	Actor->Initialize(UnlimitedGenerator);
	TestTrue(
		TEXT("fixed unlimited world accepts far explicit chunk"), Actor->RequestDataChunk(FIntVector(1000000, 0, 0)));
	TestFalse(
		TEXT("world rejects beyond expanded mesh envelope"), Actor->RequestDataChunk(FIntVector((1 << 22) - 1, 0, 0)));
	Actor->SetTestViewLocation(FVector(32000000.0, 0, 0));
	Actor->Tick(0.016f);
	TestTrue(TEXT("camera demands far fixed-lattice chunk"), Actor->IsChunkDemanded(FIntVector(1000000, 0, 0)));
	Actor->Shutdown();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}
#endif
