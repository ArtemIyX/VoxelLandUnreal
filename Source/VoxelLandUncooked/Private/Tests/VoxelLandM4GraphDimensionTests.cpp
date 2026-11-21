#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/VoxelLandM4GraphDimension.h"
#include "Graph/VoxelLandM4StageAnalysis.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Misc/AutomationTest.h"
#include <initializer_list>

namespace
{
constexpr EAutomationTestFlags M4GraphDimensionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandGraphNodeRecord M4GraphDimensionNode(uint32 Number, EVoxelLandBuiltinNode Kind)
{
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(1, Number, 0, 0);
	Node.OperatorId = VoxelLandBuiltinNodeId(Kind);
	FVoxelLandBuiltinNodeSpec Spec;
	if (!FVoxelLandBuiltinCatalog::Find(Kind, Spec))
	{
		return Node;
	}
	for (int32 Index = 0; Index < Spec.Pins.Num(); ++Index)
	{
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = FGuid(2, Number, uint32(Index + 1), 0);
		Pin.Type = Spec.Pins[Index].Type;
		Pin.bOutput = Spec.Pins[Index].bOutput;
		Node.Pins.Add(MoveTemp(Pin));
	}
	return Node;
}

FVoxelLandGraphLinkRecord M4GraphDimensionLink(
	const FVoxelLandGraphNodeRecord& Source, const FVoxelLandGraphNodeRecord& Target, int32 Input)
{
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Source.Id;
	Link.SourcePin = Source.Pins.Last().Id;
	Link.TargetNode = Target.Id;
	Link.TargetPin = Target.Pins[Input].Id;
	return Link;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4GraphDimensionProofTest, "VoxelLand.M4.GraphDimension.Proof", M4GraphDimensionFlags)
bool FVoxelLandM4GraphDimensionProofTest::RunTest(const FString& Parameters)
{
	const FVoxelLandGraphNodeRecord X = M4GraphDimensionNode(1, EVoxelLandBuiltinNode::PositionX);
	const FVoxelLandGraphNodeRecord Add = M4GraphDimensionNode(2, EVoxelLandBuiltinNode::Add);
	const FVoxelLandGraphNodeRecord Z = M4GraphDimensionNode(3, EVoxelLandBuiltinNode::PositionZ);
	const FVoxelLandGraphNodeRecord Noise = M4GraphDimensionNode(4, EVoxelLandBuiltinNode::Perlin2);
	FVoxelLandGraphSnapshot Snapshot;
	Snapshot.Nodes = {Noise, Add, Z, X};
	// One producer occupies two distinct input pins. Stage dependencies deduplicate
	// the producer; semantic encoding must still preserve both pin bindings.
	Snapshot.Links = {
		M4GraphDimensionLink(X, Add, 0), M4GraphDimensionLink(X, Add, 1), M4GraphDimensionLink(Add, Noise, 0)};
	TArray<FVoxelLandM4StageNode> Result;
	FVoxelLandError Error = VoxelLandM4GraphDimension::CollectProven2DBranch(Snapshot, Noise.Id, Result);
	TestFalse(TEXT("known 2D branch is accepted"), bool(Error));
	TestEqual(TEXT("unrelated Z node excluded"), Result.Num(), 3);
	if (Result.Num() == 3)
	{
		TestTrue(TEXT("sorted X node"), Result[0].Id == X.Id);
		TestTrue(TEXT("sorted Add node"), Result[1].Id == Add.Id);
		TestEqual(TEXT("repeated producer is deduplicated"), Result[1].Dependencies.Num(), 1);
		TestTrue(
			TEXT("Noise references Add"), Result[2].Dependencies.Num() == 1 && Result[2].Dependencies[0] == Add.Id);
		FVoxelLandM4StageOutput Output;
		Output.Name = TEXT("control");
		Output.Producer = Noise.Id;
		const TArray<FVoxelLandM4StageOutput> Outputs = {Output};
		TArray<FGuid> Closure;
		Error = VoxelLandM4StageAnalysis::CollectGlobalDependencies(Result, Outputs, TEXT("control"), Closure);
		TestFalse(TEXT("proven branch accepts M4 dependency validation"), bool(Error));
		TestEqual(TEXT("closure includes all three nodes"), Closure.Num(), 3);
	}
	Snapshot.Nodes.Swap(0, 3);
	Snapshot.Links.Swap(0, 2);
	TArray<FVoxelLandM4StageNode> Reordered;
	Error = VoxelLandM4GraphDimension::CollectProven2DBranch(Snapshot, Noise.Id, Reordered);
	TestFalse(TEXT("reordered source accepted"), bool(Error));
	if (Reordered.Num() == 3 && Result.Num() == 3)
	{
		for (int32 Index = 0; Index < Result.Num(); ++Index)
		{
			TestTrue(TEXT("same proof after source reordering"),
				Result[Index].Id == Reordered[Index].Id && Result[Index].Dependencies == Reordered[Index].Dependencies);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4GraphDimensionRejectTest, "VoxelLand.M4.GraphDimension.FailClosed", M4GraphDimensionFlags)
bool FVoxelLandM4GraphDimensionRejectTest::RunTest(const FString& Parameters)
{
	const FVoxelLandGraphNodeRecord X = M4GraphDimensionNode(1, EVoxelLandBuiltinNode::PositionX);
	const FVoxelLandGraphNodeRecord Add = M4GraphDimensionNode(2, EVoxelLandBuiltinNode::Add);
	FVoxelLandGraphSnapshot Snapshot;
	Snapshot.Nodes = {X, Add};
	Snapshot.Links = {M4GraphDimensionLink(X, Add, 0)};
	TArray<FVoxelLandM4StageNode> Result;
	FVoxelLandM4StageNode Sentinel;
	Sentinel.Id = FGuid(9, 0, 0, 0);
	Result.Add(Sentinel);
	auto Reject = [&](const TCHAR* Label, const FVoxelLandGraphSnapshot& Source, const FGuid& Root) {
		const FVoxelLandError Error = VoxelLandM4GraphDimension::CollectProven2DBranch(Source, Root, Result);
		TestTrue(Label, bool(Error));
		TestFalse(TEXT("failure has a diagnostic"), Error.Message.IsEmpty());
		TestTrue(TEXT("failure preserves output"), Result.Num() == 1 && Result[0].Id == Sentinel.Id);
	};
	for (EVoxelLandBuiltinNode Kind :
		{EVoxelLandBuiltinNode::PositionZ, EVoxelLandBuiltinNode::HeightToDensity, EVoxelLandBuiltinNode::Perlin3,
			EVoxelLandBuiltinNode::Sphere, EVoxelLandBuiltinNode::SmoothDensity, EVoxelLandBuiltinNode::DensityOutput})
	{
		FVoxelLandGraphSnapshot Unsafe;
		const FVoxelLandGraphNodeRecord Node = M4GraphDimensionNode(3, Kind);
		Unsafe.Nodes = {Node};
		Reject(TEXT("unsupported 3D, filter or output builtin rejected"), Unsafe, Node.Id);
	}
	FVoxelLandGraphSnapshot Broken = Snapshot;
	Broken.Nodes[0].OperatorId = FGuid(0xabcdef, 2, 3, 4);
	Reject(TEXT("provider in branch rejected"), Broken, Add.Id);
	Broken = Snapshot;
	Broken.Nodes[0].OperatorId = FGuid(0x564c4d33, 1, 0, 999);
	Reject(TEXT("unknown builtin rejected"), Broken, Add.Id);
	Broken = Snapshot;
	Broken.Nodes[0].OperatorVersion = 2;
	Reject(TEXT("unsupported builtin version rejected"), Broken, Add.Id);
	Broken = Snapshot;
	const FVoxelLandGraphNodeRecord Z = M4GraphDimensionNode(3, EVoxelLandBuiltinNode::PositionZ);
	Broken.Nodes.Add(Z);
	Broken.Links.Add(M4GraphDimensionLink(Z, Add, 1));
	Reject(TEXT("Z propagated through arithmetic rejected"), Broken, Add.Id);
	Broken = Snapshot;
	const FVoxelLandGraphNodeRecord Reroute = M4GraphDimensionNode(4, EVoxelLandBuiltinNode::RerouteFloat);
	Broken.Nodes.Add(Reroute);
	Broken.Links.Add(M4GraphDimensionLink(Add, Reroute, 0));
	Broken.Links.Add(M4GraphDimensionLink(Reroute, Add, 1));
	Reject(TEXT("graph cycle rejected"), Broken, Add.Id);
	Broken = Snapshot;
	Broken.Links[0].SourceNode = Add.Id;
	Reject(TEXT("incorrect pin ownership rejected"), Broken, Add.Id);
	Broken = Snapshot;
	const FVoxelLandGraphLinkRecord DuplicateLink = Broken.Links[0];
	Broken.Links.Add(DuplicateLink);
	Reject(TEXT("multiple sources to input rejected"), Broken, Add.Id);
	Broken = Snapshot;
	Broken.Nodes[0].Pins[0].bInvalidDefault = true;
	Reject(TEXT("invalid default rejected"), Broken, Add.Id);
	Broken = Snapshot;
	Broken.Nodes[0].Pins[0].Type = EVoxelLandGraphValueType::Bool;
	Reject(TEXT("pin type mismatch rejected"), Broken, Add.Id);
	Reject(TEXT("missing root rejected"), Snapshot, FGuid(10, 0, 0, 0));
	return true;
}

#endif
