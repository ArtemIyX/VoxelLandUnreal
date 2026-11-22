#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/AutomationTest.h"

// These fixed analytic expectations protect the existing M4 stage ordering
// when new cave generation is introduced: source density is saturated before
// the 3x3x3 filter. This is intentionally independent of the M5 design.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CompatibilityQuantizationTest,
	"VoxelLand.M5.Compatibility.M4SaturationBeforeSmoothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5CompatibilityQuantizationTest::RunTest(const FString& Parameters)
{
	int16 PositivePlain = 0, PositiveSmooth = 0, NegativePlain = 0, NegativeSmooth = 0;
	FString Failure;
	const bool bGenerated = Async(EAsyncExecution::ThreadPool, [&]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			Failure = TEXT("could not validate flat-field program");
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(0, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			Failure = TEXT("could not create flat height grid");
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, Generator))
		{
			Failure = TEXT("could not create M4 generator");
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Generator->CreateWorker(Worker) || !Worker)
		{
			Failure = TEXT("could not create worker on its owning thread");
			return false;
		}
		FVoxelLandChunkKey Key;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Plain, Smooth;
		if (Generator->GenerateChunk(Key, *Worker, Plain)
			|| Generator->GenerateSmoothedChunkCancellable(Key, *Worker, true, 1.f, Smooth, [] { return false; })
			|| !Plain || !Smooth || Plain->Density.Num() != FVoxelLandChunkData::SampleCount
			|| Smooth->Density.Num() != FVoxelLandChunkData::SampleCount)
		{
			Failure = TEXT("could not generate positive-height samples");
			return false;
		}
		constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
		const int32 PositiveIndex = 1 + Axis * (1 + Axis * 33); // z = +32 voxels
		PositivePlain = Plain->Density[PositiveIndex];
		PositiveSmooth = Smooth->Density[PositiveIndex];
		Key.Coordinate.Z = -1;
		if (Generator->GenerateChunk(Key, *Worker, Plain)
			|| Generator->GenerateSmoothedChunkCancellable(Key, *Worker, true, 1.f, Smooth, [] { return false; })
			|| !Plain || !Smooth || Plain->Density.Num() != FVoxelLandChunkData::SampleCount
			|| Smooth->Density.Num() != FVoxelLandChunkData::SampleCount)
		{
			Failure = TEXT("could not generate negative-height samples");
			return false;
		}
		const int32 NegativeIndex = 1 + Axis * (1 + Axis * 1); // z = -32 voxels
		NegativePlain = Plain->Density[NegativeIndex];
		NegativeSmooth = Smooth->Density[NegativeIndex];
		return true;
	}).Get();
	if (!TestTrue(FString::Printf(TEXT("flat-field generation succeeded: %s"), *Failure), bGenerated))
	{
		return false;
	}
	// Each horizontal plane is constant. At +32 the three vertical planes are
	// 31744, 32767, 32767: their mean is exactly 32426. At -32 they are
	// -31744, -32768, -32768: their mean is -32426 2/3, rounded to -32427.
	TestEqual(TEXT("positive source saturates before filtering"), int32(PositivePlain), 32767);
	TestEqual(TEXT("positive filter averages saturated neighbors"), int32(PositiveSmooth), 32426);
	TestEqual(TEXT("negative source saturates before filtering"), int32(NegativePlain), -32768);
	TestEqual(TEXT("negative filter rounds saturated average"), int32(NegativeSmooth), -32427);
	return true;
}

#endif
