#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CaptureTest, "VoxelLand.M5.Authoring.Capture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5CaptureTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	FVoxelLandM4ControlConfig* Controls[] = {&Config->Continental, &Config->Mountain, &Config->Peaks};
	for (FVoxelLandM4ControlConfig* Control : Controls)
	{
		Control->Graph = NewObject<UVoxelLandTerrainGraph>(Config);
		FVoxelLandM4CurveAuthoringKey First, Last;
		Last.InputQ16 = 65535;
		Control->CurveKeys = {First, Last};
	}
	FString Error;
	FVoxelLandM5WorldSnapshot Snapshot;
	Snapshot.Base.Seed = -7654;
	TestFalse(TEXT("M5 capture requires an enabled stage"), Config->CaptureM5Snapshot(Snapshot, Error));
	TestEqual(TEXT("disabled capture retains previous output"), Snapshot.Base.Seed, int64(-7654));
	Config->Caves.bEnabled = true;
	Config->Caves.StageId = FGuid(1, 2, 3, 4);
	Config->Caves.Bounds.MaxExclusive.X = 3000;
	if (!TestTrue(TEXT("network authoring is captured"), Config->CaptureM5Snapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("base seed captured"), Snapshot.Base.Seed, Config->Seed);
	TestEqual(TEXT("bounds copied"), Snapshot.Caves.Bounds.MaxExclusive.X, int64(3000));
	TestTrue(TEXT("M5 snapshot matches"), Config->MatchesM5Snapshot(Snapshot));
	FVoxelLandM4WorldSnapshot M4Snapshot;
	TestFalse(TEXT("M4 path still rejects enabled cave"), Config->CaptureSnapshot(M4Snapshot, Error));

	// Each mutation probes a distinct nested record. Reflected comparison also
	// covers future UPROPERTY fields without a manually maintained field list.
	Config->Caves.Capsule.RadiusCm++;
	TestFalse(TEXT("capsule edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Capsule.RadiusCm--;
	Config->Caves.Topology.MaxLoops++;
	TestFalse(TEXT("topology edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Topology.MaxLoops--;
	Config->Caves.Pockets.ThresholdQ16++;
	TestFalse(TEXT("pocket edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Pockets.ThresholdQ16--;
	Config->Caves.Pockets.AmplitudeCm++;
	TestFalse(TEXT("pocket amplitude edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Pockets.AmplitudeCm--;
	Config->Caves.Protection.bProtectAll = true;
	TestFalse(TEXT("protection edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Protection.bProtectAll = false;
	Config->Caves.Water.LevelCm++;
	TestFalse(TEXT("water edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Water.LevelCm--;
	Config->Caves.Limits.MaxValidationWork++;
	TestFalse(TEXT("resource edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Caves.Limits.MaxValidationWork--;
	Config->Seed++;
	TestFalse(TEXT("base edit is stale"), Config->MatchesM5Snapshot(Snapshot));
	Config->Seed--;
	TestTrue(TEXT("restoring settings restores freshness"), Config->MatchesM5Snapshot(Snapshot));
	Snapshot.Manifest.Composition++;
	TestFalse(TEXT("unknown captured manifest is stale"), Config->MatchesM5Snapshot(Snapshot));
	Snapshot.Manifest.Composition--;
	Snapshot.FormatVersion++;
	TestFalse(TEXT("unknown snapshot version is stale"), Config->MatchesM5Snapshot(Snapshot));
	Snapshot.FormatVersion--;

	Config->Caves.bEnabled = false;
	Config->Caves.Pockets.bEnabled = true;
	TestFalse(TEXT("changing enabled stages invalidates old snapshot"), Config->MatchesM5Snapshot(Snapshot));
	TestTrue(TEXT("pocket-only authoring is captured"), Config->CaptureM5Snapshot(Snapshot, Error));
	TestTrue(TEXT("pocket-only snapshot matches"), Config->MatchesM5Snapshot(Snapshot));
#if WITH_EDITOR
	TestTrue(TEXT("pocket-only preview snapshot matches"), Config->MatchesM5PreviewSnapshot(Snapshot));
#endif
	Config->Caves.Pockets.bEnabled = false;
	TestFalse(TEXT("disabling M5 invalidates M5 snapshot"), Config->MatchesM5Snapshot(Snapshot));
	TestTrue(TEXT("disabling M5 restores M4 capture"), Config->CaptureSnapshot(M4Snapshot, Error));

	Config->Caves.bEnabled = true;
	Config->VoxelSizeCm = 0;
	Snapshot.Base.Seed = 123456;
	TestFalse(TEXT("invalid M4 base rejects M5 capture"), Config->CaptureM5Snapshot(Snapshot, Error));
	TestEqual(TEXT("base capture failure retains M5 output"), Snapshot.Base.Seed, int64(123456));
	return true;
}
#endif
