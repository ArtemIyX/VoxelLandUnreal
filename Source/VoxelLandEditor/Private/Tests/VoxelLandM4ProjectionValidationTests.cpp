#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/VoxelLandM4ControlGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ProjectionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

void M4ProjectionFixture(UVoxelLandM4ControlGraph& Graph)
{
	FGuid* Outputs[] = {&Graph.ContinentalOutputId, &Graph.MountainOutputId, &Graph.PeaksOutputId};
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandGraphNodeRecord Value, Output;
		Value.Id = FGuid(1, 0, 0, I + 1);
		Value.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Value.Pins.SetNum(2);
		Value.Pins[0].Id = FGuid(2, 0, I + 1, 1);
		Value.Pins[1].Id = FGuid(2, 0, I + 1, 2);
		Value.Pins[1].bOutput = true;
		Output.Id = FGuid(3, 0, 0, I + 1);
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = FGuid(4, 0, 0, I + 1);
		*Outputs[I] = Output.Id;
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Value.Id;
		Link.SourcePin = Value.Pins[1].Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
		Graph.Nodes.Add(Value);
		Graph.Nodes.Add(Output);
		Graph.Links.Add(Link);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ProjectionValidationTest, "VoxelLand.M4.Editor.ProjectionValidation", M4ProjectionFlags)
bool FVoxelLandM4ProjectionValidationTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	M4ProjectionFixture(*Graph);
	FVoxelLandGraphSnapshot Source, Output;
	FString Error;
	if (!TestTrue(TEXT("complete fixture captures"), Graph->CaptureSnapshot(Source, Error)))
	{
		return false;
	}
	auto Project = [&](const FVoxelLandGraphSnapshot& Input) {
		return Graph->ProjectSnapshot(Input, EVoxelLandM4ControlChannel::Continental, Output, Error);
	};
	TestTrue(TEXT("valid complete source projects"), Project(Source));
	const int32 OriginalOutputCount = Output.Nodes.Num();
	FVoxelLandGraphSnapshot Bad = Source;
	Bad.Nodes[3].Id = Bad.Nodes[1].Id;
	TestFalse(TEXT("duplicate node in removed branch rejected"), Project(Bad));
	TestEqual(TEXT("failed projection preserves output"), Output.Nodes.Num(), OriginalOutputCount);
	Bad = Source;
	Bad.Nodes[3].Pins[0].Id = Bad.Nodes[1].Pins[0].Id;
	TestFalse(TEXT("duplicate pin in removed branch rejected"), Project(Bad));
	Bad = Source;
	Bad.Links.Add(Bad.Links[1]);
	TestFalse(TEXT("duplicate link into removed branch rejected"), Project(Bad));
	Bad = Source;
	Bad.Nodes[3].OperatorVersion = 2;
	TestFalse(TEXT("unsupported removed terminal version rejected"), Project(Bad));
	Bad = Source;
	Bad.Nodes[3].Pins[0].Type = EVoxelLandGraphValueType::Int32;
	TestFalse(TEXT("removed terminal type mismatch rejected"), Project(Bad));
	Bad = Source;
	FVoxelLandGraphLinkRecord A, B;
	A.SourceNode = Bad.Nodes[0].Id;
	A.SourcePin = Bad.Nodes[0].Pins[1].Id;
	A.TargetNode = Bad.Nodes[2].Id;
	A.TargetPin = Bad.Nodes[2].Pins[0].Id;
	B.SourceNode = Bad.Nodes[2].Id;
	B.SourcePin = Bad.Nodes[2].Pins[1].Id;
	B.TargetNode = Bad.Nodes[0].Id;
	B.TargetPin = Bad.Nodes[0].Pins[0].Id;
	Bad.Links.Add(A);
	Bad.Links.Add(B);
	TestFalse(TEXT("cycle outside selected output rejected"), Project(Bad));
	Bad = Source;
	FVoxelLandGraphNodeRecord Height;
	Height.Id = FGuid(5, 0, 0, 1);
	Height.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::M4HeightOutput);
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandGraphPinRecord& Pin = Height.Pins.AddDefaulted_GetRef();
		Pin.Id = FGuid(6, 0, 0, I + 1);
	}
	Bad.Nodes.Add(Height);
	TestFalse(TEXT("unassigned height terminal cannot mix modes"), Project(Bad));
	return true;
}
#endif
