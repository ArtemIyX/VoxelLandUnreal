#if WITH_DEV_AUTOMATION_TESTS
#include "../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialGraphPreviewTest, "VoxelLand.Materials.Editor.GraphPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialGraphPreviewTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Example = LoadObject<UVoxelLandWorldConfig>(
		nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/WC_GrassPatches.WC_GrassPatches"));
	if (!TestNotNull(TEXT("Grass Patches config"), Example))
	{
		return false;
	}
	UVoxelLandTerrainGraph* Material = Example->MaterialGraph.LoadSynchronous();
	if (!TestNotNull(TEXT("Grass Patches graph"), Material))
	{
		return false;
	}
	const FVoxelLandGraphNodeRecord* Noise = Material->Nodes.FindByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
		return Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::WorldNoise2D);
	});
	if (!TestNotNull(TEXT("World noise node"), Noise))
	{
		return false;
	}
	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateObject(Example, GetTransientPackage()));
	const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Material);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config.Get());

	const double Deadline = FPlatformTime::Seconds() + 15.0;
	while (Toolkit->HasM4PreviewWorkForTests() && FPlatformTime::Seconds() < Deadline)
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}
	TestTrue(*Toolkit->GetStatusForTests(), Toolkit->HasPublishedAppearanceForTests());
	const int32 Prior = Toolkit->GetPreviewPublishCountForTests();
	Toolkit->SelectMaterialProbeForTests(Config.Get(), Noise->Id, Noise->Pins.Last().Id);
	const double ProbeDeadline = FPlatformTime::Seconds() + 15.0;
	while (Toolkit->HasM4PreviewWorkForTests() && FPlatformTime::Seconds() < ProbeDeadline)
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}
	TestTrue(*Toolkit->GetStatusForTests(), Toolkit->HasMaterialProbeForTests());
	TestTrue(TEXT("Selecting a pin does not publish another terrain mesh"),
		Toolkit->GetPreviewPublishCountForTests() == Prior);
	Toolkit->Stop();
	return true;
}
#endif
