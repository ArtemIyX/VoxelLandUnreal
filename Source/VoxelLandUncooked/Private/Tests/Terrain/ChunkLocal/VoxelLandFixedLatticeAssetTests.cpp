#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4FixedAssetFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4FixedLatticeAssetTest, "VoxelLand.M4.FixedLattice.AssetValidation", M4FixedAssetFlags)
bool FVoxelLandM4FixedLatticeAssetTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	for (FVoxelLandM4ControlConfig* Control : {&Config->Continental, &Config->Mountain, &Config->Peaks})
	{
		Control->Graph = NewObject<UVoxelLandTerrainGraph>(Config);
		FVoxelLandM4CurveAuthoringKey First;
		First.InputQ16 = 0;
		FVoxelLandM4CurveAuthoringKey Last;
		Last.InputQ16 = 65535;
		Control->CurveKeys = {First, Last};
	}
	Config->bUnlimited = true;
	Config->bGlobalErosion = false;
	Config->bOverhangEnabled = true;
	Config->OverhangAmplitudeQ8 = 256;
	Config->HeightDetail.bEnabled = true;
	Config->HeightDetail.AmplitudeQ8 = 256;
	FString Error;
	TestTrue(TEXT("fixed-lattice shaping accepted"), Config->ValidateSettings(Error));

	Config->OverhangFrequencyPerCm = 0.00000001f;
	TestFalse(TEXT("active overhang requires a representable frequency"), Config->ValidateSettings(Error));
	Config->OverhangAmplitudeQ8 = 0;
	TestTrue(TEXT("zero-amplitude overhang does not require Q24 frequency"), Config->ValidateSettings(Error));
	Config->OverhangAmplitudeQ8 = 256;
	Config->bOverhangEnabled = false;
	TestTrue(TEXT("disabled overhang does not require Q24 frequency"), Config->ValidateSettings(Error));
	Config->HeightDetail.FrequencyPerCm = 0.00000001f;
	TestFalse(TEXT("active detail requires a representable frequency"), Config->ValidateSettings(Error));
	Config->HeightDetail.AmplitudeQ8 = 0;
	TestTrue(TEXT("zero-amplitude detail does not require Q24 frequency"), Config->ValidateSettings(Error));
	Config->HeightDetail.AmplitudeQ8 = 256;
	Config->HeightDetail.bEnabled = false;
	TestTrue(TEXT("disabled detail does not require Q24 frequency"), Config->ValidateSettings(Error));
	Config->bGlobalErosion = true;
	TestFalse(TEXT("fixed-lattice unlimited still rejects global erosion"), Config->ValidateSettings(Error));
	return true;
}

#endif
