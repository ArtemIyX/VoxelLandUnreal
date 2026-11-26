#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CookedCurveTest, "VoxelLand.M4.Cook.CurveIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4CookedCurveTest::RunTest(const FString& Parameters)
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
	Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
	Config->bGlobalErosion = true;
	TestTrue(
		TEXT("unlimited capture accepts stored finite erosion settings"), Config->CaptureSnapshot(Snapshot, Error));
	TestFalse(TEXT("unlimited snapshot disables global erosion"), Snapshot.Domain.bGlobalErosion);
	Config->WorldExtent = EVoxelLandWorldExtent::Finite;
	TestTrue(TEXT("finite capture restores the authored erosion preference"), Config->CaptureSnapshot(Snapshot, Error));
	TestTrue(TEXT("finite snapshot retains global erosion"), Snapshot.Domain.bGlobalErosion);
	Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
	Config->bGlobalErosion = false;
	Config->CellsX = 0;
	TestTrue(TEXT("unlimited without erosion is accepted"), Config->CaptureSnapshot(Snapshot, Error));
	TestTrue(TEXT("snapshot records unlimited mode"), Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited);
	Config->VoxelSizeCm = 134218;
	TestFalse(TEXT("unlimited rejects unsupported voxel size"), Config->CaptureSnapshot(Snapshot, Error));
	return true;
}
#endif
