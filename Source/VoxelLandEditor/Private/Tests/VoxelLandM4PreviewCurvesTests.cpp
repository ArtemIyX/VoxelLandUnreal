#if WITH_DEV_AUTOMATION_TESTS
#include "Graph/VoxelLandM4PreviewCurves.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "World/VoxelLandM4CurveValidation.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4PreviewCurvesTest, "VoxelLand.M4.Editor.PreviewCurveBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4PreviewCurvesTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4WorldSnapshot Snapshot;
	FVoxelLandM4CurveAuthoringKey A, B;
	A.InputQ16 = 0;
	A.OutputQ8 = -256;
	B.InputQ16 = 65535;
	B.OutputQ8 = 512;
	Snapshot.Continental.CurveKeys = Snapshot.Mountain.CurveKeys = Snapshot.Peaks.CurveKeys = {A, B};
	Snapshot.Mountain.CurveKeys[1].OutputQ8 = 1024;
	Snapshot.Peaks.CurveKeys[0].OutputQ8 = -512;
	FString Error;
	if (!TestTrue(
			TEXT("valid captured curves bake"), VoxelLandM4PreviewCurves::Bake(Snapshot, [] { return false; }, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(
		TEXT("continental preview curve validates"), VoxelLandM4CurveValidation::Validate(Snapshot.Continental, Error));
	TestTrue(TEXT("mountain preview curve validates"), VoxelLandM4CurveValidation::Validate(Snapshot.Mountain, Error));
	TestTrue(TEXT("peaks preview curve validates"), VoxelLandM4CurveValidation::Validate(Snapshot.Peaks, Error));
	TestEqual(TEXT("distinct endpoint retained"), Snapshot.Mountain.CookedCurve.SamplesQ8.Last(), 1024);
	const FVoxelLandM4WorldSnapshot Before = Snapshot;
	int32 Checks = 0;
	TestFalse(TEXT("cancellation rejects partial bake"),
		VoxelLandM4PreviewCurves::Bake(Snapshot, [&Checks] { return ++Checks == 2; }, Error));
	TestTrue(TEXT("cancel preserves all existing bakes"),
		Snapshot.Continental.CookedCurve.SamplesQ8 == Before.Continental.CookedCurve.SamplesQ8
			&& Snapshot.Mountain.CookedCurve.SamplesQ8 == Before.Mountain.CookedCurve.SamplesQ8
			&& Snapshot.Peaks.CookedCurve.SamplesQ8 == Before.Peaks.CookedCurve.SamplesQ8);
	Snapshot.Mountain.CurveKeys[1].InputQ16 = 0;
	TestFalse(
		TEXT("invalid edited curve fails"), VoxelLandM4PreviewCurves::Bake(Snapshot, [] { return false; }, Error));
	TestTrue(TEXT("invalid edit does not partially replace curves"),
		Snapshot.Continental.CookedCurve.SamplesQ8 == Before.Continental.CookedCurve.SamplesQ8
			&& Snapshot.Mountain.CookedCurve.SamplesQ8 == Before.Mountain.CookedCurve.SamplesQ8
			&& Snapshot.Peaks.CookedCurve.SamplesQ8 == Before.Peaks.CookedCurve.SamplesQ8);
	return true;
}
#endif
