#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ControlGridFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandProgramDraft M4ControlGridTestProgram(EVoxelLandOpcode Position)
{
	FVoxelLandProgramDraft Draft;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load;
	Load.Op = Position;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	return Draft;
}

FVoxelLandError M4ControlGridValidateOffThread(
	const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	return Async(EAsyncExecution::ThreadPool, [&Draft, &OutProgram] {
		return FVoxelLandProgram::Validate(Draft, OutProgram);
	}).Get();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ControlGridValidationTest, "VoxelLand.M4.ControlGrid.Validation", M4ControlGridFlags)
bool FVoxelLandM4ControlGridValidationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	FVoxelLandError Error = M4ControlGridValidateOffThread(M4ControlGridTestProgram(EVoxelLandOpcode::LoadX), Program);
	if (!TestFalse(TEXT("2D program validates"), bool(Error)) || !Program)
	{
		AddError(FString::Printf(TEXT("2D program validation failed: %s"), *Error.Message));
		return false;
	}
	TestFalse(TEXT("X program accepted as 2D"), bool(VoxelLandM4ControlGrid::ValidateProgram(*Program)));
	Error = M4ControlGridValidateOffThread(M4ControlGridTestProgram(EVoxelLandOpcode::LoadZ), Program);
	if (!TestFalse(TEXT("3D program validates for existing VM"), bool(Error)) || !Program)
	{
		AddError(FString::Printf(TEXT("3D program validation failed: %s"), *Error.Message));
		return false;
	}
	Error = VoxelLandM4ControlGrid::ValidateProgram(*Program);
	TestTrue(TEXT("Z dependency rejected"), Error.Code == EVoxelLandErrorCode::InvalidProgram);
	TestEqual(TEXT("reported Z instruction"), Error.Instruction, 0);
	FVoxelLandProgramDraft Filtered = M4ControlGridTestProgram(EVoxelLandOpcode::LoadX);
	Filtered.FormatVersion = 2;
	Filtered.Constants = {1.f, 0.5f};
	Filtered.SmoothFilter.bPresent = true;
	Filtered.SmoothFilter.Enabled.Index = 0;
	Filtered.SmoothFilter.Strength.Index = 1;
	Error = M4ControlGridValidateOffThread(Filtered, Program);
	if (!TestFalse(TEXT("filtered VM program validates"), bool(Error)) || !Program)
	{
		AddError(FString::Printf(TEXT("filtered program validation failed: %s"), *Error.Message));
		return false;
	}
	TestTrue(TEXT("filter rejected even if not executed by raw VM"),
		bool(VoxelLandM4ControlGrid::ValidateProgram(*Program)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ControlGridEvaluationTest, "VoxelLand.M4.ControlGrid.Evaluation", M4ControlGridFlags)
bool FVoxelLandM4ControlGridEvaluationTest::RunTest(const FString& Parameters)
{
	TArray<uint16> Samples;
	TArray<uint16> Cancelled = {17};
	TArray<uint16> Invalid = {19};
	EVoxelLandErrorCode CancellationCode = EVoxelLandErrorCode::None;
	EVoxelLandErrorCode InvalidCode = EVoxelLandErrorCode::None;
	const bool bSucceeded = Async(EAsyncExecution::ThreadPool, [&Samples, &Cancelled, &Invalid, &CancellationCode,
																   &InvalidCode]() {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(M4ControlGridTestProgram(EVoxelLandOpcode::LoadX), Program))
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
		if (VoxelLandM4ControlGrid::Evaluate(*Generator, *Worker, -1, -20, 1, 2, 1, Samples, [] { return false; }))
		{
			return false;
		}
		int32 Checks = 0;
		CancellationCode = VoxelLandM4ControlGrid::Evaluate(*Generator, *Worker, 0, 0, 1, 300, 1, Cancelled, [&Checks] {
			return ++Checks >= 3;
		}).Code;
		InvalidCode = VoxelLandM4ControlGrid::Evaluate(*Generator, *Worker, 16777216, 0, 1, 1, 1, Invalid, [] {
			return false;
		}).Code;
		return true;
	}).Get();
	if (!TestTrue(TEXT("worker evaluation succeeds"), bSucceeded))
	{
		return false;
	}
	const TArray<uint16> Expected = {0, 32768, 65535, 0, 32768, 65535};
	TestTrue(TEXT("row-major coordinates and quantization"), Samples == Expected);
	TestTrue(TEXT("mid-evaluation cancellation reported"), CancellationCode == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancelled output unchanged"), Cancelled == TArray<uint16>({17}));
	TestTrue(TEXT("inexact float coordinate rejected"), InvalidCode == EVoxelLandErrorCode::NonFiniteCoordinate);
	TestTrue(TEXT("invalid output unchanged"), Invalid == TArray<uint16>({19}));
	return true;
}

#endif
