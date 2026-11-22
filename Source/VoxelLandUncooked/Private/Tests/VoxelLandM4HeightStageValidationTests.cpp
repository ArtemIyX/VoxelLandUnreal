#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4SavedWorldPreflight.h"
namespace
{
constexpr EAutomationTestFlags M4HeightStageValidationFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4HeightStageValidationTest,
	"VoxelLand.M4.HeightStage.ProjectionConsistency", M4HeightStageValidationFlags)
bool FVoxelLandM4HeightStageValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4WorldSnapshot Snapshot;
	Snapshot.HeightStage.Version = 1;
	Snapshot.HeightStage.OutputNodeId = FGuid(10, 0, 0, 0);
	Snapshot.HeightStage.ContinentalInputId = FGuid(11, 0, 0, 1);
	Snapshot.HeightStage.MountainInputId = FGuid(11, 0, 0, 2);
	Snapshot.HeightStage.PeaksInputId = FGuid(11, 0, 0, 3);
	const FGuid Inputs[] = {Snapshot.HeightStage.ContinentalInputId, Snapshot.HeightStage.MountainInputId,
		Snapshot.HeightStage.PeaksInputId};
	FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	FVoxelLandGraphNodeRecord SourceNode;
	SourceNode.Id = FGuid(20, 0, 0, 0);
	SourceNode.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	SourceNode.Pins.SetNum(2);
	SourceNode.Pins[0].Id = FGuid(22, 0, 0, 0);
	SourceNode.Pins[1].Id = FGuid(21, 0, 0, 0);
	SourceNode.Pins[1].bOutput = true;
	FVoxelLandGraphNodeRecord CommonNode = SourceNode;
	CommonNode.Id = FGuid(30, 0, 0, 0);
	CommonNode.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::RerouteFloat);
	CommonNode.Pins[0].Id = FGuid(31, 0, 0, 0);
	CommonNode.Pins[1].Id = FGuid(32, 0, 0, 0);
	FVoxelLandGraphLinkRecord CommonLink;
	CommonLink.SourceNode = SourceNode.Id;
	CommonLink.SourcePin = SourceNode.Pins[1].Id;
	CommonLink.TargetNode = CommonNode.Id;
	CommonLink.TargetPin = CommonNode.Pins[0].Id;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FVoxelLandGraphNodeRecord Output;
		Output.Id = Snapshot.HeightStage.OutputNodeId;
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = Inputs[Index];
		FVoxelLandGraphLinkRecord Selected;
		Selected.SourceNode = SourceNode.Id;
		Selected.SourcePin = SourceNode.Pins[1].Id;
		Selected.TargetNode = Output.Id;
		Selected.TargetPin = Inputs[Index];
		Channels[Index]->Source.Nodes = {SourceNode, CommonNode, Output};
		Channels[Index]->Source.Links = {CommonLink, Selected};
	}
	FString Error;
	TestTrue(TEXT("three projections with distinct selected inputs are accepted"),
		VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	Snapshot.Mountain.Source.Links[1].SourceNode = CommonNode.Id;
	Snapshot.Mountain.Source.Links[1].SourcePin = CommonNode.Pins[1].Id;
	TestTrue(TEXT("channel-specific output connections may differ"),
		VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	Snapshot.Peaks.Source.Nodes[0].Pins[0].FloatDefault = 0.25f;
	TestFalse(TEXT("a projection from a different source graph is rejected"),
		VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	TestFalse(TEXT("saved snapshot preflight rejects mixed graphs"),
		VoxelLandM4SavedWorldPreflight::ValidateSnapshot(Snapshot, Error));
	Snapshot.Peaks.Source.Nodes[0].Pins[0].FloatDefault = 0.f;
	Snapshot.Peaks.Source.Links[0].SourcePin = CommonNode.Pins[1].Id;
	TestFalse(
		TEXT("differing nonterminal links are rejected"), VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	Snapshot.Peaks.Source.Links[0] = CommonLink;
	FVoxelLandGraphFunctionSnapshot Function;
	Function.AssetPath = TEXT("/Game/Functions/Example.Example");
	Snapshot.Continental.Source.Functions.Add(Function);
	TestFalse(TEXT("differing function dependencies are rejected"),
		VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	return true;
}
#endif
