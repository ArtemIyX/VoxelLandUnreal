#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4SavedWorldPreflight.h"
namespace
{
constexpr EAutomationTestFlags M4SavedWorldPreflightFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4SavedWorldPreflightTest, "VoxelLand.M4.SavedWorld.Admission", M4SavedWorldPreflightFlags)
bool FVoxelLandM4SavedWorldPreflightTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4SavedWorld Saved;
	Saved.Identity.CanonicalBytes.SetNumZeroed(8);
	FString Error;
	TestTrue(TEXT("bounded metadata admitted for worker validation"),
		VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.HorizontalRadius = 9;
	TestFalse(TEXT("excessive horizontal demand rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.HorizontalRadius = 1;
	Saved.Snapshot.Continental.Source.Functions.SetNum(17);
	TestFalse(TEXT("too many graph dependencies rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.Continental.Source.Functions.SetNum(1);
	Saved.Snapshot.Continental.Source.Functions[0].Nodes.SetNum(129);
	TestFalse(TEXT("nested dependency node limit enforced"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.Continental.Source.Functions.Empty();
	Saved.Snapshot.Mountain.Payload.ExternalOperators.SetNum(1);
	TestFalse(TEXT("unsupported external provider rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	Saved.Snapshot.Mountain.Payload.ExternalOperators.Empty();
	Saved.Snapshot.Peaks.CookedCurve.SamplesQ8.SetNum(4098);
	TestFalse(TEXT("oversized baked curve rejected"), VoxelLandM4SavedWorldPreflight::Validate(Saved, Error));
	return true;
}
#endif
