#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandWorldKind.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"

namespace
{
constexpr EAutomationTestFlags WorldCompilerFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
const FGuid WorldCompilerProviderId(0x7c9571ed, 0x60404c57, 0xb7062aba, 0x31d6a3a1);

class FWorldCompilerTestWorker final : public IVoxelLandOperatorWorker
{
public:
	bool Evaluate(TConstArrayView<const void*>, FVoxelLandOperatorOutput&, int32, FString&) override { return false; }
};

class FWorldCompilerTestProvider final : public IVoxelLandOperator
{
public:
	EVoxelLandWorldRequirement Requirement = EVoxelLandWorldRequirement::FiniteGlobal;
	FGuid Id() const override { return WorldCompilerProviderId; }
	int32 Version() const override { return 1; }
	EVoxelLandWorldRequirement WorldRequirement() const override { return Requirement; }
	TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
	EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
	uint32 MaximumWorkerBytes() const override { return sizeof(FWorldCompilerTestWorker); }
	bool ValidateConfiguration(TConstArrayView<uint8> Config, FString&) const override { return Config.IsEmpty(); }
	TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8>, uint64, FString&) const override
	{
		return MakeUnique<FWorldCompilerTestWorker>();
	}
};

FVoxelLandGraphNodeRecord WorldCompilerNode(FGuid Operator, uint32 Id, int32 Inputs, bool bOutput)
{
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(0x979, 1, 2, Id);
	Node.OperatorId = Operator;
	for (int32 I = 0; I < Inputs + int32(bOutput); ++I)
	{
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = FGuid(0x987, 1, Id, uint32(I + 1));
		Pin.bOutput = bOutput && I == Inputs;
		Node.Pins.Add(Pin);
	}
	return Node;
}

FVoxelLandGraphSnapshot WorldCompilerGraph(bool bProvider)
{
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldCompilerNode(
		VoxelLandBuiltinNodeId(bProvider ? EVoxelLandBuiltinNode::PositionX : EVoxelLandBuiltinNode::FloatConstant), 1,
		bProvider ? 0 : 1, true));
	if (bProvider)
	{
		Graph.Nodes.Add(WorldCompilerNode(WorldCompilerProviderId, 2, 1, true));
	}
	Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput), 3, 1, false));
	for (int32 I = 1; I < Graph.Nodes.Num(); ++I)
	{
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Graph.Nodes[I - 1].Id;
		Link.SourcePin = Graph.Nodes[I - 1].Pins.Last().Id;
		Link.TargetNode = Graph.Nodes[I].Id;
		Link.TargetPin = Graph.Nodes[I].Pins[0].Id;
		Graph.Links.Add(Link);
	}
	return Graph;
}

FVoxelLandGraphSnapshot WorldCompilerNoiseGraph(EVoxelLandBuiltinNode Kind, bool b3D)
{
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionX), 1, 0, true));
	Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionY), 2, 0, true));
	const int32 Dimensions = b3D ? 3 : 2;
	if (b3D)
	{
		Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionZ), 3, 0, true));
	}
	const int32 NoiseIndex = Graph.Nodes.Num();
	Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(Kind), 4, Dimensions + 4, true));
	Graph.Nodes[NoiseIndex].Pins[Dimensions].FloatDefault = 0.01f;
	Graph.Nodes[NoiseIndex].Pins[Dimensions + 1].Type = EVoxelLandGraphValueType::Int32;
	Graph.Nodes[NoiseIndex].Pins[Dimensions + 1].IntDefault = 3;
	Graph.Nodes[NoiseIndex].Pins[Dimensions + 2].FloatDefault = 0.5f;
	Graph.Nodes[NoiseIndex].Pins[Dimensions + 3].FloatDefault = 2.f;
	Graph.Nodes.Add(WorldCompilerNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput), 5, 1, false));
	for (int32 I = 0; I < Dimensions; ++I)
	{
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Graph.Nodes[I].Id;
		Link.SourcePin = Graph.Nodes[I].Pins.Last().Id;
		Link.TargetNode = Graph.Nodes[NoiseIndex].Id;
		Link.TargetPin = Graph.Nodes[NoiseIndex].Pins[I].Id;
		Graph.Links.Add(Link);
	}
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Graph.Nodes[NoiseIndex].Id;
	Link.SourcePin = Graph.Nodes[NoiseIndex].Pins.Last().Id;
	Link.TargetNode = Graph.Nodes.Last().Id;
	Link.TargetPin = Graph.Nodes.Last().Pins[0].Id;
	Graph.Links.Add(Link);
	return Graph;
}

TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> WorldCompilerBudget(
	int64 Bytes = 1024 * 1024, int64 Work = 1000000000)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Result;
	FVoxelLandM5Budget::TryCreate(Bytes, Work, Result);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandWorldCompilerTest, "VoxelLand.WorldModes.Compiler.FrozenProvider", WorldCompilerFlags)
bool FVoxelLandWorldCompilerTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	FString Error;
	auto Provider = MakeShared<FWorldCompilerTestProvider, ESPMode::ThreadSafe>();
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandUncooked"), Provider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
		AddError(Error);
		return false;
	}
	{
		const FVoxelLandOperatorRegistrySnapshot Frozen = FVoxelLandOperatorRegistry::Snapshot();
		// Metadata is frozen with the provider registration. Later mutation of the
		// originating object cannot downgrade the same snapshot's requirement.
		Provider->Requirement = EVoxelLandWorldRequirement::SampleLocal;
		FVoxelLandProgramDraft Draft;
		FVoxelLandWorldCapabilityDiagnostic Capability;
		FVoxelLandGraphDiagnostic GraphDiagnostic;
		const FVoxelLandGraphSnapshot Graph = WorldCompilerGraph(true);
		TestFalse(TEXT("infinite rejects frozen finite requirement"),
			FVoxelLandGraphCompiler::CompileForWorld(Graph, EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestEqual(TEXT("requirement failure"), int32(Capability.Code),
			int32(EVoxelLandWorldCapabilityError::FiniteWorldRequired));
		TestTrue(TEXT("diagnostic identifies provider node"), Capability.NodeId == Graph.Nodes[1].Id);
		TestTrue(TEXT("rejected compile clears draft"), Draft.Instructions.IsEmpty());
		TestTrue(TEXT("finite compiles same frozen graph"),
			FVoxelLandGraphCompiler::CompileForWorld(Graph, EVoxelLandWorldKind::Finite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestTrue(TEXT("finite result contains a program"), !Draft.Instructions.IsEmpty());
		TestTrue(TEXT("infinite accepts built-in local graph"),
			FVoxelLandGraphCompiler::CompileForWorld(WorldCompilerGraph(false), EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		for (const auto& Case : {TPair<EVoxelLandBuiltinNode, bool>(EVoxelLandBuiltinNode::Perlin2, false),
				 TPair<EVoxelLandBuiltinNode, bool>(EVoxelLandBuiltinNode::Value3, true)})
		{
			const FVoxelLandGraphSnapshot Noise = WorldCompilerNoiseGraph(Case.Key, Case.Value);
			TestFalse(TEXT("infinite rejects noise unsupported by current backend"),
				FVoxelLandGraphCompiler::CompileForWorld(Noise, EVoxelLandWorldKind::Infinite, Frozen,
					WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
			TestFalse(TEXT("backend diagnostic does not guess a source node"), Capability.NodeId.IsValid());
			TestFalse(TEXT("graph diagnostic does not guess a source node"), GraphDiagnostic.NodeId.IsValid());
			TestTrue(
				TEXT("backend diagnostic identifies instruction"), Capability.Message.Contains(TEXT("instruction ")));
			TestTrue(TEXT("diagnostic names actual backend"), Capability.Message.Contains(TEXT("fixed-lattice")));
			TestTrue(TEXT("rejected backend clears draft"), Draft.Instructions.IsEmpty());
		}
		FVoxelLandGraphSnapshot RepeatedNoise = WorldCompilerNoiseGraph(EVoxelLandBuiltinNode::Perlin2, false);
		FVoxelLandGraphNodeRecord UnusedNoise = RepeatedNoise.Nodes[2];
		UnusedNoise.Id = FGuid(0x979, 1, 2, 99);
		for (int32 PinIndex = 0; PinIndex < UnusedNoise.Pins.Num(); ++PinIndex)
		{
			UnusedNoise.Pins[PinIndex].Id = FGuid(0x987, 1, 99, uint32(PinIndex + 1));
		}
		RepeatedNoise.Nodes.Insert(UnusedNoise, 0);
		TestFalse(TEXT("repeated opcode rejects unsupported connected noise"),
			FVoxelLandGraphCompiler::CompileForWorld(RepeatedNoise, EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestFalse(TEXT("repeated opcode does not blame first unrelated node"), Capability.NodeId.IsValid());
		TestTrue(
			TEXT("repeated opcode diagnostic reports instruction"), Capability.Message.Contains(TEXT("instruction ")));

		FVoxelLandGraphSnapshot NestedNoise = WorldCompilerNoiseGraph(EVoxelLandBuiltinNode::Perlin2, false);
		FVoxelLandGraphFunctionSnapshot NoiseFunction;
		NoiseFunction.AssetPath = TEXT("/Game/Test/Noise.Noise");
		NoiseFunction.Nodes = {NestedNoise.Nodes[0], NestedNoise.Nodes[1], NestedNoise.Nodes[2]};
		NoiseFunction.Links = {NestedNoise.Links[0], NestedNoise.Links[1]};
		NoiseFunction.OutputNode = NoiseFunction.Nodes[2].Id;
		NoiseFunction.OutputPin = NoiseFunction.Nodes[2].Pins.Last().Id;
		FVoxelLandGraphNodeRecord NoiseCall = WorldCompilerNode(VoxelLandGraphFunctionCallId(), 30, 0, true);
		NoiseCall.FunctionPath = FSoftObjectPath(NoiseFunction.AssetPath);
		NoiseCall.Pins[0].Name = TEXT("Result");
		FVoxelLandGraphNodeRecord NoiseOutput = NestedNoise.Nodes.Last();
		FVoxelLandGraphLinkRecord NoiseLink;
		NoiseLink.SourceNode = NoiseCall.Id;
		NoiseLink.SourcePin = NoiseCall.Pins[0].Id;
		NoiseLink.TargetNode = NoiseOutput.Id;
		NoiseLink.TargetPin = NoiseOutput.Pins[0].Id;
		NestedNoise.Nodes = {NoiseCall, NoiseOutput};
		NestedNoise.Links = {NoiseLink};
		NestedNoise.Functions = {NoiseFunction};
		TestFalse(TEXT("nested unsupported noise rejects infinite backend"),
			FVoxelLandGraphCompiler::CompileForWorld(NestedNoise, EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestTrue(TEXT("nested failure comes from fixed backend"), Capability.Message.Contains(TEXT("fixed-lattice")));
		TestFalse(TEXT("nested backend diagnostic does not guess node"), Capability.NodeId.IsValid());
		TestTrue(TEXT("nested backend diagnostic does not guess function"), Capability.FunctionPath.IsEmpty());
		TestTrue(TEXT("nested backend failure clears draft"), Draft.Instructions.IsEmpty());
		const FVoxelLandGraphSnapshot FixedNoise = WorldCompilerNoiseGraph(EVoxelLandBuiltinNode::Value2, false);
		TestTrue(TEXT("infinite accepts supported fixed-lattice Value2"),
			FVoxelLandGraphCompiler::CompileForWorld(FixedNoise, EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		FVoxelLandGraphSnapshot Nested;
		FVoxelLandGraphNodeRecord Call = WorldCompilerNode(VoxelLandGraphFunctionCallId(), 20, 0, false);
		Call.FunctionPath = FSoftObjectPath(TEXT("/Game/Test/A.A"));
		Nested.Nodes.Add(Call);
		FVoxelLandGraphFunctionSnapshot A;
		A.AssetPath = TEXT("/Game/Test/A.A");
		FVoxelLandGraphNodeRecord NestedCall = WorldCompilerNode(VoxelLandGraphFunctionCallId(), 21, 0, false);
		NestedCall.FunctionPath = FSoftObjectPath(TEXT("/Game/Test/B.B"));
		A.Nodes.Add(NestedCall);
		FVoxelLandGraphFunctionSnapshot B;
		B.AssetPath = TEXT("/Game/Test/B.B");
		B.Nodes.Add(WorldCompilerNode(WorldCompilerProviderId, 22, 1, true));
		Nested.Functions = {A, B};
		TestFalse(TEXT("nested finite-global provider is rejected for infinite world"),
			FVoxelLandGraphCompiler::CompileForWorld(Nested, EVoxelLandWorldKind::Infinite, Frozen,
				WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestEqual(TEXT("nested failure identifies finite-global requirement"), int32(Capability.Code),
			int32(EVoxelLandWorldCapabilityError::FiniteWorldRequired));
		TestTrue(TEXT("nested provider node reported"), Capability.NodeId == B.Nodes[0].Id);
		TestEqual(TEXT("nested function path reported"), Capability.FunctionPath, B.AssetPath);
		TestFalse(TEXT("missing provider cannot compile"),
			FVoxelLandGraphCompiler::CompileForWorld(WorldCompilerGraph(true), EVoxelLandWorldKind::Infinite,
				FVoxelLandOperatorRegistrySnapshot(), WorldCompilerBudget().ToSharedRef(), NeverCancel, Draft,
				Capability, GraphDiagnostic));
		TestEqual(TEXT("missing capability is explicit"), int32(Capability.Code),
			int32(EVoxelLandWorldCapabilityError::UnknownCapability));
		TestFalse(TEXT("zero work fails before compile"),
			FVoxelLandGraphCompiler::CompileForWorld(Graph, EVoxelLandWorldKind::Finite, Frozen,
				WorldCompilerBudget(1024 * 1024, 0).ToSharedRef(), NeverCancel, Draft, Capability, GraphDiagnostic));
		TestEqual(TEXT("budget failure is explicit"), int32(Capability.Code),
			int32(EVoxelLandWorldCapabilityError::BudgetExceeded));
		const auto AlwaysCancel = [] {
			return true;
		};
		TestFalse(TEXT("cancellation fails"),
			FVoxelLandGraphCompiler::CompileForWorld(Graph, EVoxelLandWorldKind::Finite, Frozen,
				WorldCompilerBudget().ToSharedRef(), AlwaysCancel, Draft, Capability, GraphDiagnostic));
		TestEqual(
			TEXT("cancellation is explicit"), int32(Capability.Code), int32(EVoxelLandWorldCapabilityError::Cancelled));
	}
	TestTrue(
		TEXT("unregister test provider"), FVoxelLandOperatorRegistry::Unregister(WorldCompilerProviderId, 1, Error));
	return true;
}
#endif
