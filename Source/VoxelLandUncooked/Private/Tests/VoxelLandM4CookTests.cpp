#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CookedCurveTest, "VoxelLand.M4.Cook.CurveIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4CookedCurveTest::RunTest(const FString& Parameters)
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
	TestFalse(TEXT("missing bake rejected"), Config->ValidateBakedCurves(Error));
	TArray<int32> Samples;
	Samples.Init(0, 4097);
	if (!TestTrue(TEXT("baked curves accepted"), Config->SetBakedCurves(Samples, Samples, Samples, 1, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("fresh bake validates"), Config->ValidateBakedCurves(Error));
	FVoxelLandM4WorldSnapshot Snapshot;
	TestTrue(TEXT("snapshot captures baked values"), Config->CaptureSnapshot(Snapshot, Error));
	Samples[0] = 7;
	TestEqual(TEXT("baked data owns samples"), Config->Continental.CookedCurve.SamplesQ8[0], 0);
	TArray<int32> Short;
	Short.Init(0, 4096);
	TestFalse(TEXT("failed replacement rejected"), Config->SetBakedCurves(Short, Samples, Samples, 1, Error));
	TestTrue(TEXT("failed replacement preserves prior data"), Config->ValidateBakedCurves(Error));
	Config->Continental.CurveKeys[1].OutputQ8 = 1;
	TestFalse(TEXT("key edit invalidates bake"), Config->ValidateBakedCurves(Error));
	TestFalse(TEXT("key edit invalidates snapshot"), Config->MatchesSnapshot(Snapshot));
	Config->Continental.CurveKeys[1].OutputQ8 = 0;
	Config->Continental.CookedCurve.SamplesQ8[1] = 1;
	TestFalse(TEXT("corrupt LUT fails content hash"), Config->ValidateBakedCurves(Error));
	Config->Continental.CookedCurve = Snapshot.Continental.CookedCurve;
	Config->Peaks.CookedCurve.BakeVersion = 2;
	TestFalse(TEXT("unknown bake version rejected"), Config->ValidateBakedCurves(Error));
	Config->ClearBakedCurves();
	TestFalse(TEXT("cleared bake rejected"), Config->ValidateBakedCurves(Error));
	Config->bUnlimited = true;
	Config->bGlobalErosion = true;
	TestFalse(TEXT("unlimited rejects global erosion"), Config->CaptureSnapshot(Snapshot, Error));
	Config->bGlobalErosion = false;
	Config->CellsX = 0;
	TestTrue(TEXT("unlimited without erosion is accepted"), Config->CaptureSnapshot(Snapshot, Error));
	TestTrue(TEXT("snapshot records unlimited mode"), Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited);
	Config->VoxelSizeCm = 134218;
	TestFalse(TEXT("unlimited rejects unsupported voxel size"), Config->CaptureSnapshot(Snapshot, Error));
	return true;
}
#endif
