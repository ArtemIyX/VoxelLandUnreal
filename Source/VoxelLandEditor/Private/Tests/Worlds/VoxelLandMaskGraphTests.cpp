#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNodeSpec.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Meshing/VoxelLandMaterialSignalSource.h"
#include "Graph/Compilation/VoxelLandMaskGraphProgram.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Misc/AutomationTest.h"
#include "Worlds/Assets/VoxelLandMaskGraphFactory.h"

class FVoxelLandMaskGraphTestFixture final : public IVoxelLandMaterialSignalSource
{
public:
	static constexpr double ExpectedImportedWeight = 0.375;
	const FGuid Signal{34, 5, 7, 9};
	FVoxelLandGraphSnapshot Graph;

	FVoxelLandMaskGraphTestFixture() { Graph.GraphRole = EVoxelLandGraphRole::Mask; }

	int32 Add(EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		verify(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
		const int32 Index = Graph.Nodes.AddDefaulted();
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes[Index];
		Node.Id = NextGuid();
		Node.OperatorId = Spec.OperatorId;
		Node.OperatorVersion = Spec.OperatorVersion;
		for (const FVoxelLandBuiltinPinSpec& Source : Spec.Pins)
		{
			FVoxelLandGraphPinRecord& Pin = Node.Pins.AddDefaulted_GetRef();
			Pin.Id = NextGuid();
			Pin.Name = Source.Name;
			Pin.Type = Source.Type;
			Pin.bOutput = Source.bOutput;
			Pin.FloatDefault = FCString::Atof(*Source.DefaultLiteral);
		}
		return Index;
	}

	void Link(int32 Source, int32 Target)
	{
		const FVoxelLandGraphNodeRecord& From = Graph.Nodes[Source];
		const FVoxelLandGraphNodeRecord& To = Graph.Nodes[Target];
		FVoxelLandGraphLinkRecord& Wire = Graph.Links.AddDefaulted_GetRef();
		Wire.SourceNode = From.Id;
		Wire.SourcePin = From.Pins.Last().Id;
		Wire.TargetNode = To.Id;
		Wire.TargetPin = To.Pins[0].Id;
	}

	bool SampleTerrain(FGuid Output, const FVector3d&, double& Out) const override
	{
		if (Output != Signal)
		{
			return false;
		}
		Out = ExpectedImportedWeight;
		return true;
	}

	bool SampleWorld(FName, const FVector2d&, double&) const override { return false; }

private:
	FGuid NextGuid() { return FGuid(6, 1, 7, ++NextId); }
	uint32 NextId = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaskGraphProgramTest, "VoxelLand.PCG.MaskGraph.Program",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaskGraphProgramTest::RunTest(const FString&)
{
	const UVoxelLandMaskGraphFactory* Factory = GetDefault<UVoxelLandMaskGraphFactory>();
	TestEqual(TEXT("Dedicated factory label"), Factory->GetDisplayName().ToString(), FString(TEXT("Mask Graph")));

	FVoxelLandMaskGraphTestFixture Fixture;
	const int32 Slope = Fixture.Add(EVoxelLandBuiltinNode::SurfaceSlope);
	const int32 Remap = Fixture.Add(EVoxelLandBuiltinNode::MaterialRemap);
	Fixture.Graph.Nodes[Remap].Pins[1].FloatDefault = 0.0;
	Fixture.Graph.Nodes[Remap].Pins[2].FloatDefault = 60.0;
	const int32 SlopeOutput = Fixture.Add(EVoxelLandBuiltinNode::FloatOutput);
	FVoxelLandGraphOutputs::SetName(Fixture.Graph.Nodes[SlopeOutput], TEXT("Slope Weight"));
	Fixture.Link(Slope, Remap);
	Fixture.Link(Remap, SlopeOutput);

	const int32 TerrainInput = Fixture.Add(EVoxelLandBuiltinNode::TerrainSignal);
	FVoxelLandMaterialGraphProgram::WriteGuid(Fixture.Signal, Fixture.Graph.Nodes[TerrainInput].Configuration);
	const int32 SignalOutput = Fixture.Add(EVoxelLandBuiltinNode::FloatOutput);
	FVoxelLandGraphOutputs::SetName(Fixture.Graph.Nodes[SignalOutput], TEXT("Imported Weight"));
	Fixture.Link(TerrainInput, SignalOutput);

	TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> Program;
	FString Error;
	if (!FVoxelLandMaskGraphProgram::Compile(Fixture.Graph, Program, Error))
	{
		AddError(Error);
		return false;
	}

	FVoxelLandMaterialGraphPoint Point;
	Point.Position = FVector3d(-3200, 2900, 1200);
	Point.SlopeDegrees = 30.0;
	double Weight = -1.0;
	const FGuid SlopeId = Fixture.Graph.Nodes[SlopeOutput].Id;
	const FGuid SignalId = Fixture.Graph.Nodes[SignalOutput].Id;
	TestEqual(TEXT("Two independent outputs"), Program->GetOutputs().Num(), 2);
	TestTrue(TEXT("Slope output succeeds"), Program->Evaluate(SlopeId, Point, Weight));
	TestEqual(TEXT("Slope weight at midpoint"), Weight, 0.5);
	Point.SlopeDegrees = 80.0;
	TestTrue(TEXT("Slope output succeeds outside the authored range"), Program->Evaluate(SlopeId, Point, Weight));
	TestEqual(TEXT("Mask output upper bound"), Weight, 1.0);
	TestTrue(TEXT("Declared terrain dependency"), Program->GetTerrainSignals().Contains(Fixture.Signal));
	TestFalse(
		TEXT("Missing terrain dependency is an explicit sample failure"), Program->Evaluate(SignalId, Point, Weight));
	Point.Signals = &Fixture;
	TestTrue(TEXT("Bound terrain output succeeds"), Program->Evaluate(SignalId, Point, Weight));
	TestEqual(TEXT("Bound terrain output value"), Weight, FVoxelLandMaskGraphTestFixture::ExpectedImportedWeight);

	FVoxelLandGraphOutputs::SetName(Fixture.Graph.Nodes[SlopeOutput], TEXT("Steepness"));
	TestTrue(TEXT("Renaming an output recompiles"), FVoxelLandMaskGraphProgram::Compile(Fixture.Graph, Program, Error));
	TestEqual(TEXT("Renaming preserves GUID"), Program->GetOutputs()[0].Id, SlopeId);
	TestEqual(TEXT("Renaming updates display name"), Program->GetOutputs()[0].Name, FName(TEXT("Steepness")));
	TestTrue(TEXT("Existing output GUID still evaluates"), Program->Evaluate(SlopeId, Point, Weight));

	FVoxelLandGraphOutputs::SetName(Fixture.Graph.Nodes[SignalOutput], TEXT("Steepness"));
	TestFalse(
		TEXT("Duplicate outputs are rejected"), FVoxelLandMaskGraphProgram::Compile(Fixture.Graph, Program, Error));
	TestTrue(TEXT("Failure names the offending output"), Error.Contains(TEXT("Steepness")));
	TestTrue(TEXT("Rejected compile preserves accepted program"), Program->Evaluate(SlopeId, Point, Weight));
	return true;
}

#endif
