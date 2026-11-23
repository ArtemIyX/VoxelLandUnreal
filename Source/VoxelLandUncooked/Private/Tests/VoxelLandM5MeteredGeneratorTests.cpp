#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredGeneratorTest, "VoxelLand.M5.Generator.MeteredSetup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5MeteredGeneratorTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadZ;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		FVoxelLandParameterSlot Parameter;
		Parameter.Name = TEXT("scale");
		Draft.Parameters.Add(Parameter);
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Foreign;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Budget);
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Foreign);
		auto NeverCancel = [] { return false; };
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Legacy;
		if (FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), NeverCancel, Program).Code
			!= EVoxelLandM5ErrorCode::None || FVoxelLandProgram::Validate(Draft, Legacy) || !Program || !Legacy)
		{
			AddError(TEXT("Program fixture setup failed"));
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		int64 Before = 0, Peak = 0, Work = 0;
		Budget->GetUsage(Before, Peak, Work);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Output;
		TestEqual(TEXT("metered setup succeeds"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), NeverCancel, Output).Code), int32(EVoxelLandM5ErrorCode::None));
		if (!Output) { return false; }
		int64 Live = 0;
		Budget->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("generator retains admission"), Live > Before);
		const FVoxelLandGenerator* Previous = Output.Get();
		TestEqual(TEXT("foreign budget rejected"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Foreign.ToSharedRef(), NeverCancel, Output).Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("foreign budget preserves output"), Output.Get() == Previous);
		Settings.Program = Legacy;
		TestEqual(TEXT("unmetered program rejected"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), NeverCancel, Output).Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		Settings.Program = Program;
		Settings.Profile.FloatOverrides.Add(FString::ChrN(257, TEXT('a')), 1.0f);
		TestEqual(TEXT("long name rejected"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), NeverCancel, Output).Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		Settings.Profile.FloatOverrides.Empty();
		Settings.Profile.FloatOverrides.Add(TEXT("scale"), 1.0f);
		int64 LiveBeforeCancel = 0;
		Budget->GetUsage(LiveBeforeCancel, Peak, Work);
		int32 Polls = 0;
		auto CancelDuringSetup = [&Polls] { return ++Polls == 4; };
		TestEqual(TEXT("cancel inside parameter setup"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), CancelDuringSetup, Output).Code), int32(EVoxelLandM5ErrorCode::Cancelled));
		Budget->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("in-flight cancellation preserves output"), Output.Get() == Previous);
		TestEqual(TEXT("in-flight cancellation releases admission"), Live, LiveBeforeCancel);
		Settings.Profile.FloatOverrides.Empty();
		auto Cancel = [] { return true; };
		TestEqual(TEXT("cancellation"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), Cancel, Output).Code), int32(EVoxelLandM5ErrorCode::Cancelled));
		TestTrue(TEXT("failures preserve output"), Output.Get() == Previous);
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Remaining;
		Budget->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("reserve remaining bytes"), Budget->TryReserveBytes(8 * 1024 * 1024 - Live, Remaining));
		TestEqual(TEXT("memory exhaustion"), int32(FVoxelLandGenerator::CreateMetered(
			Settings, Budget.ToSharedRef(), NeverCancel, Output).Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("memory failure preserves output"), Output.Get() == Previous);
		Remaining.Reset();
		Output.Reset();
		Budget->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("generator lease released"), Live, Before);
		return true;
	}).Get();
}
#endif
