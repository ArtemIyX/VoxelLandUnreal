#if WITH_DEV_AUTOMATION_TESTS

#include "../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNodeSpec.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandMaskPreviewFixture final
{
public:
	static constexpr double Weight = 0.375;

	static void Populate(UVoxelLandTerrainGraph& Graph)
	{
		Graph.GraphRole = EVoxelLandGraphRole::Mask;
		Graph.Nodes.Reserve(2);
		FVoxelLandGraphNodeRecord& WeightNode = Add(Graph, EVoxelLandBuiltinNode::FloatConstant);
		WeightNode.Pins[0].FloatDefault = Weight;
		FVoxelLandGraphNodeRecord& Output = Add(Graph, EVoxelLandBuiltinNode::FloatOutput);
		FVoxelLandGraphOutputs::SetName(Output, TEXT("Preview Weight"));
		FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
		Link.SourceNode = WeightNode.Id;
		Link.SourcePin = WeightNode.Pins.Last().Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
	}

private:
	static FVoxelLandGraphNodeRecord& Add(UVoxelLandTerrainGraph& Graph, EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		verify(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
		FVoxelLandGraphNodeRecord& Record = Graph.Nodes.AddDefaulted_GetRef();
		Record.Id = FGuid::NewGuid();
		Record.OperatorId = Spec.OperatorId;
		Record.OperatorVersion = Spec.OperatorVersion;
		for (const FVoxelLandBuiltinPinSpec& Argument : Spec.Pins)
		{
			FVoxelLandGraphPinRecord& Pin = Record.Pins.AddDefaulted_GetRef();
			Pin.Id = FGuid::NewGuid();
			Pin.Name = Argument.Name;
			Pin.Type = Argument.Type;
			Pin.bOutput = Argument.bOutput;
			Pin.FloatDefault = FCString::Atof(*Argument.DefaultLiteral);
		}
		return Record;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaskGraphPreviewTest, "VoxelLand.PCG.MaskGraph.EditorPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaskGraphPreviewTest::RunTest(const FString&)
{
	const UVoxelLandWorldConfig* Example =
		LoadObject<UVoxelLandWorldConfig>(nullptr, TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"));
	if (!TestNotNull(TEXT("Finite Rolling Hills config"), Example))
	{
		return false;
	}

	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateObject(Example, GetTransientPackage()));
	const TStrongObjectPtr<UVoxelLandTerrainGraph> Mask(NewObject<UVoxelLandTerrainGraph>(GetTransientPackage()));
	FVoxelLandMaskPreviewFixture::Populate(*Mask);
	Config->MaskGraph = Mask.Get();

	const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Mask.Get());
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config.Get());

	const double Deadline = FPlatformTime::Seconds() + 15.0;
	while (Toolkit->HasM4PreviewWorkForTests() && FPlatformTime::Seconds() < Deadline)
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}

	TestFalse(TEXT("The preview job finished"), Toolkit->HasM4PreviewWorkForTests());
	TestTrue(*Toolkit->GetStatusForTests(), Toolkit->HasMaterialProbeForTests());
	TestTrue(TEXT("The published grayscale raster reports the authored weight over completed terrain"),
		Toolkit->GetStatusForTests().Contains(TEXT("Range 0.375 to 0.375.")));
	TestEqual(TEXT("Mask preview publishes a scalar image without publishing a terrain mesh"),
		Toolkit->GetPreviewPublishCountForTests(), 0);
	Toolkit->Stop();
	return true;
}

#endif
