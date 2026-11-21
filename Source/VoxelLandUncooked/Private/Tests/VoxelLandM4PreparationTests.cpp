#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "World/VoxelLandM4Preparation.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4PreparationSafetyTest, "VoxelLand.M4.Preparation.Safety",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4PreparationSafetyTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4WorldSnapshot Snapshot;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Output;
	FString Error;
	const bool Result = Async(EAsyncExecution::ThreadPool, [&] {
		return VoxelLandM4Preparation::Prepare(Snapshot, nullptr, [] { return false; }, Output, Error);
	}).Get();
	TestFalse(TEXT("preparation requires a reservation"), Result);
	TestFalse(TEXT("failure explains missing reservation"), Error.IsEmpty());
	TestFalse(TEXT("failure publishes no generator"), Output.IsValid());
	return true;
}
#endif
