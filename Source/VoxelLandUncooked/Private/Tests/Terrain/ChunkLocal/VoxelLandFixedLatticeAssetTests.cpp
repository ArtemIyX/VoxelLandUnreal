#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
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
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	for (FVoxelLandM4ControlConfig* Control : {&Config->Continental, &Config->Mountain, &Config->Peaks})
	{
		Control->Graph = NewObject<UVoxelLandTerrainGraph>(Config);
		FVoxelLandM4CurveAuthoringKey First;
		First.InputQ16 = 0;
		FVoxelLandM4CurveAuthoringKey Last;
		Last.InputQ16 = 65535;
		Control->CurveKeys = {First, Last};
	}
	Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
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
	FVoxelLandM4WorldSnapshot Snapshot;
	TestTrue(TEXT("infinite config captures with its finite erosion preference enabled"),
		Config->CaptureSnapshot(Snapshot, Error));
	TestFalse(TEXT("captured infinite domain excludes erosion"), Snapshot.Domain.bGlobalErosion);
	TestTrue(TEXT("captured fixed-lattice domain remains admissible"), !VoxelLandM4Domain::Validate(Snapshot.Domain));
	Snapshot.Domain.bGlobalErosion = true;
	TestTrue(TEXT("fixed-lattice backend rejects an unlimited domain that requests erosion"),
		bool(VoxelLandM4Domain::Validate(Snapshot.Domain)));
	return true;
}

#endif
