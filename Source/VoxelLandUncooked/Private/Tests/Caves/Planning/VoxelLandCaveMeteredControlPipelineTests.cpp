#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainSettings.h"
#include "Data/Terrain/Erosion/VoxelLandErosionSettings.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipelineInputs.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveMeteredControlPipeline.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5PipelineFixtureLease final : public FVoxelLandM4GridLease
{
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredControlPipelineTest, "VoxelLand.M5.Runtime.MeteredControlPipeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeteredControlPipelineTest::RunTest(const FString& Parameters)
{
	TArray<int32> CurveSamples;
	CurveSamples.Init(0, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Failure;
	if (!TestTrue(TEXT("curve"), FVoxelLandM4CurveLut::Create(CurveSamples, 0, 0, Curve, Failure)))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(4 * 1024 * 1024, 10000000, Budget)))
	{
		return false;
	}
	bool bOk = false, bAtomic = false, bCancelled = false, bMidCancelled = false, bFinalCancelled = false;
	bool bGridLease = false, bReleased = false;
	TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> WeakProcess;
	int32 Height = 0;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		Draft.Constants = {0.f};
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadConstant;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::Create({Program, 1, FVoxelLandZoneProfile(), FVoxelLandBackendOptions()}, Generator))
		{
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Generator->CreateWorker(Worker))
		{
			return false;
		}
		FVoxelLandM4DomainSettings Domain;
		Domain.CellsX = Domain.CellsY = 1;
		Domain.OriginXCm = Domain.OriginYCm = -100;
		Domain.GridSpacingCm = 100;
		Domain.bGlobalErosion = false;
		FVoxelLandM4ErosionSettings Erosion;
		const FGuid Stage;
		const FVoxelLandM4ControlPipelineInputs Inputs{Domain, {*Generator, *Worker, *Curve},
			{*Generator, *Worker, *Curve}, {*Generator, *Worker, *Curve}, 5, 42, Stage, 0, 0, Erosion};
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Process =
			MakeShared<FVoxelLandM5PipelineFixtureLease, ESPMode::ThreadSafe>();
		WeakProcess = Process;
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		uint64 Hash = 77;
		int32 SuccessfulPolls = 0;
		const FVoxelLandM5Diagnostic Success = VoxelLand::M5::MeteredControlPipeline::Generate(
			Inputs, Budget.ToSharedRef(), Process,
			[&SuccessfulPolls] {
				++SuccessfulPolls;
				return false;
			},
			Grid, Hash);
		Process.Reset();
		int64 Live = 0, Peak = 0, Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		bOk = Success.Code == EVoxelLandM5ErrorCode::None && Grid.IsValid() && Grid->Sample(-100, -100, Height);
		bGridLease = bOk && Live > 0 && WeakProcess.IsValid();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
		if (!FVoxelLandM5Budget::TryCreate(1, 10000000, Tiny))
		{
			return false;
		}
		const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Original = Grid;
		const uint64 OriginalHash = Hash;
		const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> AnotherProcess =
			MakeShared<FVoxelLandM5PipelineFixtureLease, ESPMode::ThreadSafe>();
		const FVoxelLandM5Diagnostic Exhausted = VoxelLand::M5::MeteredControlPipeline::Generate(
			Inputs, Tiny.ToSharedRef(), AnotherProcess, [] { return false; }, Grid, Hash);
		bAtomic = Exhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Grid == Original && Hash == OriginalHash;
		const FVoxelLandM5Diagnostic Cancelled = VoxelLand::M5::MeteredControlPipeline::Generate(
			Inputs, Budget.ToSharedRef(), AnotherProcess, [] { return true; }, Grid, Hash);
		bCancelled = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Grid == Original && Hash == OriginalHash;
		Budget->GetUsage(Live, Peak, Work);
		const int64 LiveBeforeCancel = Live;
		int32 Polls = 0;
		const FVoxelLandM5Diagnostic MidCancel = VoxelLand::M5::MeteredControlPipeline::Generate(
			Inputs, Budget.ToSharedRef(), AnotherProcess, [&Polls] { return ++Polls >= 7; }, Grid, Hash);
		Budget->GetUsage(Live, Peak, Work);
		bMidCancelled = MidCancel.Code == EVoxelLandM5ErrorCode::Cancelled && Grid == Original && Hash == OriginalHash
					 && Live == LiveBeforeCancel;
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> FinalProcess =
			MakeShared<FVoxelLandM5PipelineFixtureLease, ESPMode::ThreadSafe>();
		const TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> WeakFinalProcess = FinalProcess;
		int32 FinalPolls = 0;
		const FVoxelLandM5Diagnostic FinalCancel = VoxelLand::M5::MeteredControlPipeline::Generate(
			Inputs, Budget.ToSharedRef(), FinalProcess,
			[&FinalPolls, SuccessfulPolls] { return ++FinalPolls == SuccessfulPolls; }, Grid, Hash);
		FinalProcess.Reset();
		Budget->GetUsage(Live, Peak, Work);
		bFinalCancelled = SuccessfulPolls > 0 && FinalPolls == SuccessfulPolls
					   && FinalCancel.Code == EVoxelLandM5ErrorCode::Cancelled && Grid == Original
					   && Hash == OriginalHash && Live == LiveBeforeCancel && !WeakFinalProcess.IsValid();
		Grid.Reset();
		// Original deliberately keeps the reservation alive until it leaves scope.
		Budget->GetUsage(Live, Peak, Work);
		bReleased = Live > 0 && WeakProcess.IsValid();
		return true;
	}).Get();
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("worker fixture setup"), bSetup);
	TestTrue(TEXT("finite pipeline produces a grid"), bOk);
	TestEqual(TEXT("constant controls preserve base height"), Height, 5);
	TestTrue(TEXT("grid retains both leases"), bGridLease && bReleased);
	TestEqual(TEXT("final grid release restores live budget"), Live, int64(0));
	TestFalse(TEXT("final grid release releases process lease"), WeakProcess.IsValid());
	TestTrue(TEXT("memory failure leaves outputs unchanged"), bAtomic);
	TestTrue(TEXT("cancellation leaves outputs unchanged"), bCancelled);
	TestTrue(TEXT("producer cancellation releases scratch and preserves outputs"), bMidCancelled);
	TestTrue(TEXT("one-shot final cancellation preserves outputs and releases leases"), bFinalCancelled);
	return bSetup;
}
#endif
