#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/TerrainStages/VoxelLandHeightGraphStageAnalysis.h"
#include "Misc/AutomationTest.h"
#include <initializer_list>

namespace
{
constexpr EAutomationTestFlags M4StageTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM4StageNode M4StageTestNode(
	uint32 Id, EVoxelLandM4StageKind Stage, std::initializer_list<FGuid> Dependencies = {})
{
	FVoxelLandM4StageNode Node;
	Node.Id = FGuid(Id, 0, 0, 0);
	Node.Stage = Stage;
	for (const FGuid& Dependency : Dependencies)
	{
		Node.Dependencies.Add(Dependency);
	}
	return Node;
}

FVoxelLandM4StageOutput M4StageTestOutput(const TCHAR* Name, uint32 Producer)
{
	FVoxelLandM4StageOutput Output;
	Output.Name = Name;
	Output.Producer = FGuid(Producer, 0, 0, 0);
	return Output;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4StageDependenciesTest, "VoxelLand.M4.Stage.Dependencies", M4StageTestFlags)
bool FVoxelLandM4StageDependenciesTest::RunTest(const FString& Parameters)
{
	const FGuid One(1, 0, 0, 0), Two(2, 0, 0, 0), Three(3, 0, 0, 0), Four(4, 0, 0, 0);
	TArray<FVoxelLandM4StageNode> Nodes = {M4StageTestNode(4, EVoxelLandM4StageKind::Global, {Three, One}),
		M4StageTestNode(3, EVoxelLandM4StageKind::Global, {Two}), M4StageTestNode(2, EVoxelLandM4StageKind::Global),
		M4StageTestNode(1, EVoxelLandM4StageKind::Global), M4StageTestNode(5, EVoxelLandM4StageKind::Sample, {Four}),
		M4StageTestNode(6, EVoxelLandM4StageKind::Global)};
	const TArray<FVoxelLandM4StageOutput> Outputs = {
		M4StageTestOutput(TEXT("Height"), 4), M4StageTestOutput(TEXT("Other"), 6)};
	TArray<FGuid> Result;
	FVoxelLandError Error = VoxelLandM4StageAnalysis::CollectGlobalDependencies(Nodes, Outputs, TEXT("Height"), Result);
	TestFalse(TEXT("valid graph succeeds"), bool(Error));
	const TArray<FGuid> Expected = {One, Two, Three, Four};
	TestTrue(
		TEXT("transitive closure is dependency-first and excludes downstream and unrelated nodes"), Result == Expected);
	Nodes.Swap(0, 5);
	Nodes[5].Dependencies.Swap(0, 1);
	TArray<FGuid> Reordered;
	Error = VoxelLandM4StageAnalysis::CollectGlobalDependencies(Nodes, Outputs, TEXT("Height"), Reordered);
	TestFalse(TEXT("reordered graph succeeds"), bool(Error));
	TestTrue(TEXT("node and input order do not change closure order"), Reordered == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4StageValidationTest, "VoxelLand.M4.Stage.Validation", M4StageTestFlags)
bool FVoxelLandM4StageValidationTest::RunTest(const FString& Parameters)
{
	const FGuid One(1, 0, 0, 0), Two(2, 0, 0, 0), Missing(99, 0, 0, 0);
	const TArray<FVoxelLandM4StageOutput> Outputs = {M4StageTestOutput(TEXT("Height"), 2)};
	const TArray<FVoxelLandM4StageNode> Good = {
		M4StageTestNode(1, EVoxelLandM4StageKind::Global), M4StageTestNode(2, EVoxelLandM4StageKind::Global, {One})};
	const TArray<FGuid> Sentinel = {Missing};
	TArray<FGuid> Result = Sentinel;
	auto Reject = [&](const TCHAR* Label, const TArray<FVoxelLandM4StageNode>& Nodes,
					  const TArray<FVoxelLandM4StageOutput>& NamedOutputs, const FString& Name) {
		const FVoxelLandError Error =
			VoxelLandM4StageAnalysis::CollectGlobalDependencies(Nodes, NamedOutputs, Name, Result);
		TestTrue(Label, bool(Error));
		TestFalse(TEXT("error includes diagnostic"), Error.Message.IsEmpty());
		TestTrue(TEXT("failure does not replace prior result"), Result == Sentinel);
	};
	TArray<FVoxelLandM4StageNode> Nodes = Good;
	Nodes[1].Dependencies.Add(Missing);
	Reject(TEXT("missing producer rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[1].Dependencies.Add(One);
	Reject(TEXT("duplicate dependency rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[0].Id = Two;
	Reject(TEXT("duplicate node rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[0].Dependencies.Add(Two);
	Reject(TEXT("cycle rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes.Add(M4StageTestNode(3, EVoxelLandM4StageKind::Global, {FGuid(3, 0, 0, 0)}));
	Reject(TEXT("unreachable cycle rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[0].bUsesZ = true;
	Reject(TEXT("global Z input rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[0].Stage = EVoxelLandM4StageKind::Sample;
	Reject(TEXT("global depends on sample rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes[0].Stage = EVoxelLandM4StageKind::Filter;
	Reject(TEXT("global depends on filter rejected"), Nodes, Outputs, TEXT("Height"));
	Nodes = Good;
	Nodes[0].Stage = static_cast<EVoxelLandM4StageKind>(255);
	Reject(TEXT("invalid stage rejected"), Nodes, Outputs, TEXT("Height"));
	Reject(TEXT("missing selected output rejected"), Good, Outputs, TEXT("Other"));
	TArray<FVoxelLandM4StageOutput> InvalidOutputs = {M4StageTestOutput(TEXT(""), 2)};
	Reject(TEXT("empty output name rejected"), Good, InvalidOutputs, TEXT(""));
	InvalidOutputs = {M4StageTestOutput(TEXT("  "), 2)};
	Reject(TEXT("whitespace output name rejected"), Good, InvalidOutputs, TEXT("  "));
	InvalidOutputs = {M4StageTestOutput(TEXT("Height"), 2), M4StageTestOutput(TEXT("Height"), 1)};
	Reject(TEXT("duplicate output rejected"), Good, InvalidOutputs, TEXT("Height"));
	InvalidOutputs = {M4StageTestOutput(TEXT("Height"), 99)};
	Reject(TEXT("missing output producer rejected"), Good, InvalidOutputs, TEXT("Height"));
	Nodes = Good;
	Nodes[1].Stage = EVoxelLandM4StageKind::Sample;
	Reject(TEXT("sample output rejected"), Nodes, Outputs, TEXT("Height"));
	return true;
}

#endif
