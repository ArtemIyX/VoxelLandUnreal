#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandPCGGraphPolicy.h"

#include "VoxelLandPCGGetVoxelTerrainSettings.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Misc/AutomationTest.h"
#include "PCGGraph.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPCGGraphPolicyTest, "VoxelLand.PCG.GraphPolicy.PointOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandPCGGraphPolicyTest::RunTest(const FString&)
{
	UPCGGraph* Graph = NewObject<UPCGGraph>();
	UVoxelLandPCGGetVoxelTerrainSettings* Terrain = nullptr;
	Graph->AddNodeOfType(Terrain);
	FString Error;
	TestTrue(TEXT("Terrain point graph is supported"), FVoxelLandPCGGraphPolicy::Validate(*Graph, Error));
	UPCGStaticMeshSpawnerSettings* Spawner = nullptr;
	Graph->AddNodeOfType(Spawner);
	TestFalse(TEXT("Native scene spawner is rejected"), FVoxelLandPCGGraphPolicy::Validate(*Graph, Error));
	TestTrue(TEXT("Diagnostic names rejected setting"), Error.Contains(TEXT("PCGStaticMeshSpawnerSettings")));
	return true;
}

#endif
