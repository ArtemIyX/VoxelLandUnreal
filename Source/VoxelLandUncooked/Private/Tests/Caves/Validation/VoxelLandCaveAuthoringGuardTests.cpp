#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5AuthoringGuardTest, "VoxelLand.M5.Authoring.M4Guard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5AuthoringGuardTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	FVoxelLandM4ControlConfig* Controls[] = {&Config->Continental, &Config->Mountain, &Config->Peaks};
	for (FVoxelLandM4ControlConfig* Control : Controls)
	{
		Control->Graph = NewObject<UVoxelLandTerrainGraph>(Config);
		FVoxelLandM4CurveAuthoringKey First, Last;
		Last.InputQ16 = 65535;
		Control->CurveKeys = {First, Last};
	}
	TestFalse(TEXT("planned caves default disabled"), Config->Caves.bEnabled);
	TestFalse(TEXT("pockets default disabled"), Config->Caves.Pockets.bEnabled);
	FVoxelLandM4WorldSnapshot Snapshot;
	FString Error;
	if (!TestTrue(TEXT("disabled M5 captures the existing M4 snapshot"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("disabled M5 leaves snapshot current"), Config->MatchesSnapshot(Snapshot));
	Config->Caves.RoofThicknessCm += 100;
	TestTrue(TEXT("inactive cave settings do not invalidate M4"), Config->MatchesSnapshot(Snapshot));
	for (int32 Mode = 0; Mode != 3; ++Mode)
	{
		Config->Caves.bEnabled = Mode != 1;
		Config->Caves.Pockets.bEnabled = Mode != 0;
		FVoxelLandM4WorldSnapshot Retained;
		Retained.Seed = -98765;
		TestFalse(TEXT("enabled M5 cannot be captured as M4"), Config->CaptureSnapshot(Retained, Error));
		TestTrue(TEXT("capture explains enabled M5"), Error.Contains(TEXT("M5")));
		TestEqual(TEXT("failed capture retains output"), Retained.Seed, int64(-98765));
		TestFalse(TEXT("enabled M5 fails M4 settings validation"), Config->ValidateSettings(Error));
		TestFalse(TEXT("enabled M5 invalidates an in-flight M4 snapshot"), Config->MatchesSnapshot(Snapshot));
#if WITH_EDITOR
		TestFalse(TEXT("enabled M5 invalidates an in-flight M4 preview"), Config->MatchesPreviewSnapshot(Snapshot));
#endif
	}
	Config->Caves.bEnabled = false;
	Config->Caves.Pockets.bEnabled = false;
	TestTrue(TEXT("disabling M5 restores M4 snapshot freshness"), Config->MatchesSnapshot(Snapshot));
	return true;
}
#endif
