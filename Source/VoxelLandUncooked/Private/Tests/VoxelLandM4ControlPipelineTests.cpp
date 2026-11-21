#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlPipeline.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ControlPipelineFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandError M4ControlPipelineMakeGenerator(float Constant, bool bUsesZ,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	TUniquePtr<FVoxelLandWorkerContext>& OutWorker)
{
	FVoxelLandProgramDraft Draft;
	Draft.RegisterCount = 1;
	Draft.Constants = {Constant};
	FVoxelLandInstruction Load;
	Load.Op = bUsesZ ? EVoxelLandOpcode::LoadZ : EVoxelLandOpcode::LoadConstant;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (const FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Program))
	{
		return Error;
	}
	if (const FVoxelLandError Error = FVoxelLandGenerator::Create(
			{Program, 1, FVoxelLandZoneProfile(), FVoxelLandBackendOptions()}, OutGenerator))
	{
		return Error;
	}
	return OutGenerator->CreateWorker(OutWorker);
}

bool M4ControlPipelineMakeCurve(int32 Scale, TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& OutCurve)
{
	TArray<int32> Samples;
	Samples.Reserve(FVoxelLandM4CurveLut::SampleCount);
	for (int32 Index = 0; Index < FVoxelLandM4CurveLut::SampleCount; ++Index)
	{
		Samples.Add(Index * Scale);
	}
	FString Error;
	return FVoxelLandM4CurveLut::Create(Samples, 0, 12288, OutCurve, Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ControlPipelineTest, "VoxelLand.M4.ControlPipeline.Generate", M4ControlPipelineFlags)
bool FVoxelLandM4ControlPipelineTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> CCurve, MCurve, PCurve;
	if (!TestTrue(TEXT("continental curve"), M4ControlPipelineMakeCurve(1, CCurve))
		|| !TestTrue(TEXT("mountain curve"), M4ControlPipelineMakeCurve(2, MCurve))
		|| !TestTrue(TEXT("peaks curve"), M4ControlPipelineMakeCurve(3, PCurve)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	EVoxelLandErrorCode ZError = EVoxelLandErrorCode::None;
	EVoxelLandErrorCode CancelError = EVoxelLandErrorCode::None;
	bool bPreservedAfterZ = false, bPreservedAfterCancel = false;
	const bool bSucceeded = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> C, M, P, Invalid;
		TUniquePtr<FVoxelLandWorkerContext> CW, MW, PW, InvalidWorker;
		if (M4ControlPipelineMakeGenerator(-1.f, false, C, CW) || M4ControlPipelineMakeGenerator(0.f, false, M, MW)
			|| M4ControlPipelineMakeGenerator(1.f, false, P, PW)
			|| M4ControlPipelineMakeGenerator(0.f, true, Invalid, InvalidWorker))
		{
			return false;
		}
		FVoxelLandM4DomainSettings Domain;
		Domain.CellsX = Domain.CellsY = 1;
		Domain.OriginXCm = Domain.OriginYCm = -100;
		Domain.GridSpacingCm = 100;
		Domain.bGlobalErosion = false;
		FVoxelLandM4ErosionSettings Erosion;
		if (VoxelLandM4ControlPipeline::Generate(Domain, *C, *CW, *CCurve, *M, *MW, *MCurve, *P, *PW, *PCurve, 5, 42,
				FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; }))
		{
			return false;
		}
		const auto PriorGrid = Grid;
		const uint64 PriorHash = Hash;
		ZError = VoxelLandM4ControlPipeline::Generate(Domain, *C, *CW, *CCurve, *M, *MW, *MCurve, *Invalid,
			*InvalidWorker, *PCurve, 5, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] {
				return false;
			}).Code;
		bPreservedAfterZ = Grid == PriorGrid && Hash == PriorHash;
		int32 Checks = 0;
		CancelError = VoxelLandM4ControlPipeline::Generate(Domain, *C, *CW, *CCurve, *M, *MW, *MCurve, *P, *PW, *PCurve,
			5, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [&Checks] {
				return ++Checks == 3;
			}).Code;
		bPreservedAfterCancel = Grid == PriorGrid && Hash == PriorHash;
		return true; // worker contexts are destroyed on their creating thread
	}).Get();
	if (!TestTrue(TEXT("pipeline executes on worker thread"), bSucceeded)
		|| !TestTrue(TEXT("grid published"), Grid.IsValid()))
	{
		return false;
	}
	int32 Height = 0;
	TestTrue(TEXT("grid origin sampled"), Grid->Sample(-100, -100, Height));
	// Control values -1, 0, +1 quantize to 0, 32768, 65535. The
	// three LUT outputs are 0, 4096 and 12288 respectively.
	TestEqual(TEXT("all control maps contribute to height"), Height, 16389);
	TestTrue(TEXT("inclusive endpoint sampled"), Grid->Sample(0, 0, Height));
	TestEqual(TEXT("constant controls produce constant grid"), Height, 16389);
	TestTrue(TEXT("Z-dependent program rejected"), ZError == EVoxelLandErrorCode::InvalidProgram);
	TestTrue(TEXT("failed program leaves prior output"), bPreservedAfterZ);
	TestTrue(TEXT("cancellation reported"), CancelError == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancelled pipeline leaves prior output"), bPreservedAfterCancel);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
