#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandWorldCapabilities.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags WorldCapabilitiesTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandGraphNodeRecord WorldCapabilitiesTestNode(uint32 Id, FGuid Operator)
{
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(1, 2, 3, Id);
	Node.OperatorId = Operator;
	return Node;
}

FVoxelLandGraphNodeRecord WorldCapabilitiesTestCall(uint32 Id, const TCHAR* Path)
{
	FVoxelLandGraphNodeRecord Node = WorldCapabilitiesTestNode(Id, VoxelLandGraphFunctionCallId());
	Node.FunctionPath = FSoftObjectPath(Path);
	return Node;
}

TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> WorldCapabilitiesTestBudget(
	int64 Bytes = 1024 * 1024, int64 Work = 1000000000)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5Budget::TryCreate(Bytes, Work, Budget);
	return Budget;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandWorldCapabilitiesModesTest, "VoxelLand.WorldModes.GraphCapabilities.Modes", WorldCapabilitiesTestFlags)
bool FVoxelLandWorldCapabilitiesModesTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	const FGuid ProviderId(11, 22, 33, 44);
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldCapabilitiesTestNode(1, VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::Value3)));
	Graph.Nodes.Add(WorldCapabilitiesTestNode(2, VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity)));
	Graph.Nodes.Add(WorldCapabilitiesTestNode(3, ProviderId));
	FVoxelLandWorldOperatorCapability Capability;
	Capability.OperatorId = ProviderId;
	Capability.OperatorVersion = 1;
	Capability.Requirement = EVoxelLandWorldRequirement::FiniteGlobal;
	TArray<FVoxelLandWorldOperatorCapability> Providers{Capability};
	auto Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Finite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(
		TEXT("finite accepts finite-global provider"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::None));
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("infinite rejects finite-global provider"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::FiniteWorldRequired));
	TestTrue(TEXT("diagnostic identifies authored provider node"), Result.NodeId == Graph.Nodes[2].Id);
	Providers[0].Requirement = EVoxelLandWorldRequirement::BoundedRegion;
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("infinite accepts regional provider and bounded filter"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::None));
	Providers[0].OperatorVersion = 2;
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("provider version mismatch is unknown"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::UnknownCapability));
	Providers[0].OperatorVersion = 1;
	Providers[0].Requirement = EVoxelLandWorldRequirement::Unspecified;
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("unspecified provider requirement is invalid"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Providers[0].Requirement = static_cast<EVoxelLandWorldRequirement>(255);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Finite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("unknown provider requirement is invalid"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Providers[0].Requirement = EVoxelLandWorldRequirement::SampleLocal;
	Providers.Add(Providers[0]);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("duplicate provider declarations are invalid"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Providers.Reset();
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Finite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("unknown provider fails even finite"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::UnknownCapability));
	TestTrue(TEXT("missing provider diagnostic points to node"), Result.NodeId == Graph.Nodes[2].Id);
	FVoxelLandWorldOperatorCapability Unspecified;
	TestEqual(TEXT("provider declaration defaults to unspecified"), int32(Unspecified.Requirement),
		int32(EVoxelLandWorldRequirement::Unspecified));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldCapabilitiesFunctionsTest,
	"VoxelLand.WorldModes.GraphCapabilities.FunctionClosure", WorldCapabilitiesTestFlags)
bool FVoxelLandWorldCapabilitiesFunctionsTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	const FGuid ProviderId(123, 456, 789, 10);
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldCapabilitiesTestCall(1, TEXT("/Game/Funcs/A.A")));
	FVoxelLandGraphFunctionSnapshot A;
	A.AssetPath = TEXT("/Game/Funcs/A.A");
	A.Nodes.Add(WorldCapabilitiesTestCall(2, TEXT("/Game/Funcs/B.B")));
	FVoxelLandGraphFunctionSnapshot B;
	B.AssetPath = TEXT("/Game/Funcs/B.B");
	B.Nodes.Add(WorldCapabilitiesTestNode(3, ProviderId));
	Graph.Functions.Add(A);
	Graph.Functions.Add(B);
	FVoxelLandWorldOperatorCapability Provider;
	Provider.OperatorId = ProviderId;
	Provider.OperatorVersion = 1;
	Provider.Requirement = EVoxelLandWorldRequirement::FiniteGlobal;
	TArray<FVoxelLandWorldOperatorCapability> Providers{Provider};
	auto Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("nested finite requirement rejected"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::FiniteWorldRequired));
	TestTrue(TEXT("nested node id reported"), Result.NodeId == B.Nodes[0].Id);
	TestEqual(TEXT("containing function reported"), Result.FunctionPath, B.AssetPath);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Finite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(
		TEXT("finite accepts same function chain"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::None));
	Graph.Functions[1].Nodes.Add(WorldCapabilitiesTestCall(4, TEXT("/Game/Funcs/A.A")));
	Providers[0].Requirement = EVoxelLandWorldRequirement::SampleLocal;
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("cycle fails"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Graph.Functions[1].Nodes.Pop();
	Graph.Functions.RemoveAt(1);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(
		TEXT("missing dependency fails"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::InvalidInput));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldCapabilitiesFailureTest,
	"VoxelLand.WorldModes.GraphCapabilities.Failures", WorldCapabilitiesTestFlags)
bool FVoxelLandWorldCapabilitiesFailureTest::RunTest(const FString& Parameters)
{
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldCapabilitiesTestNode(1, VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::Value2)));
	TArray<FVoxelLandWorldOperatorCapability> Providers;
	const auto NeverCancel = [] {
		return false;
	};
	const auto AlwaysCancel = [] {
		return true;
	};
	auto Result = FVoxelLandWorldCapabilities::Validate(Graph, EVoxelLandWorldKind::Infinite, Providers,
		WorldCapabilitiesTestBudget(1024 * 1024, 0).ToSharedRef(), NeverCancel);
	TestEqual(TEXT("work budget fails before graph scanning"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::BudgetExceeded));
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), AlwaysCancel);
	TestEqual(TEXT("immediate cancellation"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::Cancelled));
	int32 Polls = 0;
	auto CancelDuringScan = [&Polls] {
		return ++Polls == 2;
	};
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), CancelDuringScan);
	TestEqual(
		TEXT("cancellation during node scan"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::Cancelled));
	Result = FVoxelLandWorldCapabilities::Validate(Graph, EVoxelLandWorldKind::Infinite, Providers,
		WorldCapabilitiesTestBudget(1, 1000000000).ToSharedRef(), NeverCancel);
	TestEqual(TEXT("scratch budget fails"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::BudgetExceeded));
	Result = FVoxelLandWorldCapabilities::Validate(Graph, static_cast<EVoxelLandWorldKind>(255), Providers,
		WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(
		TEXT("unknown world kind fails"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Graph.Nodes.Add(Graph.Nodes[0]);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("duplicate node id fails"), int32(Result.Code), int32(EVoxelLandWorldCapabilityError::InvalidInput));
	Graph.Nodes.SetNum(1);
	Graph.Nodes[0].OperatorId = FGuid(0x564c4d33, 1, 0, uint32(EVoxelLandBuiltinNode::M4HeightOutput) + 1);
	Result = FVoxelLandWorldCapabilities::Validate(
		Graph, EVoxelLandWorldKind::Infinite, Providers, WorldCapabilitiesTestBudget().ToSharedRef(), NeverCancel);
	TestEqual(TEXT("unknown future built-in is not assumed local"), int32(Result.Code),
		int32(EVoxelLandWorldCapabilityError::UnknownCapability));
	return true;
}
#endif
