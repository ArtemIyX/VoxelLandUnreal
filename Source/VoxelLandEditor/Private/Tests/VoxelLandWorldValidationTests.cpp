#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "VoxelLandWorldEditorValidation.h"

#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandInfiniteWorld.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandGraphFunctionSnapshot.h"
#include "Data/Structs/VoxelLandWorldCommonSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"
#include "Serialization/ArchiveSavePackageData.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ArchiveCookContext.h"
#include "UObject/ObjectSaveContext.h"

namespace
{
const FGuid WorldEditorFiniteProviderId(0x5f14b30e, 0xa21442c7, 0x9d1ae88b, 0xe3b1360a);

class FWorldEditorValidationWorker final : public IVoxelLandOperatorWorker
{
public:
	bool Evaluate(TConstArrayView<const void*>, FVoxelLandOperatorOutput&, int32, FString&) override { return false; }
};

class FWorldEditorValidationProvider final : public IVoxelLandOperator
{
public:
	FGuid Id() const override { return WorldEditorFiniteProviderId; }
	int32 Version() const override { return 1; }
	EVoxelLandWorldRequirement WorldRequirement() const override { return EVoxelLandWorldRequirement::FiniteGlobal; }
	TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
	EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
	uint32 MaximumWorkerBytes() const override { return sizeof(FWorldEditorValidationWorker); }
	mutable TAtomic<int32> ValidationCalls{0};
	bool ValidateConfiguration(TConstArrayView<uint8> Config, FString&) const override
	{
		++ValidationCalls;
		return Config.IsEmpty();
	}
	TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8>, uint64, FString&) const override
	{
		return MakeUnique<FWorldEditorValidationWorker>();
	}
};

FVoxelLandGraphNodeRecord WorldEditorValidationNode(FGuid Operator, uint32 Id, int32 Inputs, bool bOutput)
{
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(0x7a90, 1, 2, Id);
	Node.OperatorId = Operator;
	for (int32 Index = 0; Index < Inputs + int32(bOutput); ++Index)
	{
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = FGuid(0x7a91, 1, Id, uint32(Index + 1));
		Pin.bOutput = bOutput && Index == Inputs;
		Node.Pins.Add(Pin);
	}
	return Node;
}

FVoxelLandGraphSnapshot WorldEditorValidationNestedGraph()
{
	FVoxelLandGraphSnapshot Graph;
	FVoxelLandGraphFunctionSnapshot Function;
	Function.AssetPath = TEXT("/Game/Test/WorldEditorFunction.WorldEditorFunction");
	Function.Nodes.Add(WorldEditorValidationNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionX), 1, 0, true));
	Function.Nodes.Add(WorldEditorValidationNode(WorldEditorFiniteProviderId, 2, 1, true));
	FVoxelLandGraphLinkRecord Inner;
	Inner.SourceNode = Function.Nodes[0].Id;
	Inner.SourcePin = Function.Nodes[0].Pins.Last().Id;
	Inner.TargetNode = Function.Nodes[1].Id;
	Inner.TargetPin = Function.Nodes[1].Pins[0].Id;
	Function.Links.Add(Inner);
	Function.OutputNode = Function.Nodes[1].Id;
	Function.OutputPin = Function.Nodes[1].Pins.Last().Id;
	FVoxelLandGraphNodeRecord Call = WorldEditorValidationNode(VoxelLandGraphFunctionCallId(), 3, 0, true);
	Call.FunctionPath = FSoftObjectPath(Function.AssetPath);
	Call.Pins[0].Name = TEXT("Result");
	Graph.Nodes.Add(MoveTemp(Call));
	Graph.Nodes.Add(
		WorldEditorValidationNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput), 4, 1, false));
	FVoxelLandGraphLinkRecord Outer;
	Outer.SourceNode = Graph.Nodes[0].Id;
	Outer.SourcePin = Graph.Nodes[0].Pins[0].Id;
	Outer.TargetNode = Graph.Nodes[1].Id;
	Outer.TargetPin = Graph.Nodes[1].Pins[0].Id;
	Graph.Links.Add(Outer);
	Graph.Functions.Add(MoveTemp(Function));
	return Graph;
}

FVoxelLandGraphSnapshot WorldEditorValidationLocalGraph()
{
	FVoxelLandGraphSnapshot Graph;
	Graph.Nodes.Add(WorldEditorValidationNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant), 5, 1, true));
	Graph.Nodes.Add(
		WorldEditorValidationNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput), 6, 1, false));
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Graph.Nodes[0].Id;
	Link.SourcePin = Graph.Nodes[0].Pins[0].Id;
	Link.TargetNode = Graph.Nodes[1].Id;
	Link.TargetPin = Graph.Nodes[1].Pins[0].Id;
	Graph.Links.Add(Link);
	return Graph;
}

FVoxelLandGraphSnapshot WorldEditorValidationFilteredGraph()
{
	FVoxelLandGraphSnapshot Graph = WorldEditorValidationLocalGraph();
	Graph.Nodes.Add(WorldEditorValidationNode(VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity), 7, 3, true));
	Graph.Nodes[2].Pins[1].Type = EVoxelLandGraphValueType::Bool;
	Graph.Nodes[2].Pins[1].BoolDefault = true;
	Graph.Nodes[2].Pins[2].FloatDefault = 0.5f;
	Graph.Links[0].TargetNode = Graph.Nodes[2].Id;
	Graph.Links[0].TargetPin = Graph.Nodes[2].Pins[0].Id;
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Graph.Nodes[2].Id;
	Link.SourcePin = Graph.Nodes[2].Pins.Last().Id;
	Link.TargetNode = Graph.Nodes[1].Id;
	Link.TargetPin = Graph.Nodes[1].Pins[0].Id;
	Graph.Links.Add(Link);
	return Graph;
}

TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> WorldEditorValidationBudget()
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 1000000000, Budget);
	return Budget;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldEditorValidationTest, "VoxelLand.WorldModes.Editor.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandWorldEditorValidationTest::RunTest(const FString& Parameters)
{
	FString Error;
	const auto Provider = MakeShared<FWorldEditorValidationProvider, ESPMode::ThreadSafe>();
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandEditor"), Provider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
		AddError(Error);
		return false;
	}
	{
		const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
		FVoxelLandWorldCommonSnapshot Snapshot;
		FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source = WorldEditorValidationNestedGraph();
			FVoxelLandM4CurveAuthoringKey& First = Channel->CurveKeys.AddDefaulted_GetRef();
			First.InputQ16 = 0;
			First.OutputQ8 = -256;
			FVoxelLandM4CurveAuthoringKey& Last = Channel->CurveKeys.AddDefaulted_GetRef();
			Last.InputQ16 = 65535;
			Last.OutputQ8 = 256;
		}
		const auto NeverCancel = [] {
			return false;
		};
		TestFalse(TEXT("finite grid rejects unsupported external operator"),
			VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Finite, Providers,
				WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
		TestTrue(TEXT("finite diagnostic names unsupported provider"), Error.Contains(TEXT("external")));
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source = WorldEditorValidationLocalGraph();
		}
		TestTrue(TEXT("finite control grid accepts local constant graphs"),
			VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Finite, Providers,
				WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source = WorldEditorValidationFilteredGraph();
			TestFalse(TEXT("finite control grid rejects smooth filter"),
				VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Finite, Providers,
					WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
			TestTrue(TEXT("finite filter diagnostic explains restriction"), Error.Contains(TEXT("filter")));
			Channel->Source = WorldEditorValidationLocalGraph();
		}
		Provider->ValidationCalls.Store(0);
		FVoxelLandGraphSnapshot Standalone = WorldEditorValidationLocalGraph();
		TestTrue(TEXT("standalone built-in graph passes save preflight"),
			VoxelLandWorldEditorValidation::CanValidateSynchronously(Standalone, Error));
		Standalone.Nodes[0].OperatorId = WorldEditorFiniteProviderId;
		TestFalse(TEXT("standalone root provider is deferred before synchronous dispatch"),
			VoxelLandWorldEditorValidation::CanValidateSynchronously(Standalone, Error));
		Standalone = WorldEditorValidationNestedGraph();
		TestFalse(TEXT("standalone nested provider is deferred before synchronous dispatch"),
			VoxelLandWorldEditorValidation::CanValidateSynchronously(Standalone, Error));
		TestEqual(TEXT("standalone preflight never calls external provider"), Provider->ValidationCalls.Load(), 0);
		TestTrue(TEXT("built-in graphs pass synchronous save preflight"),
			VoxelLandWorldEditorValidation::CanValidateSynchronously(Snapshot, Error));
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source = WorldEditorValidationNestedGraph();
			TestFalse(TEXT("nested external provider is deferred before synchronous dispatch"),
				VoxelLandWorldEditorValidation::CanValidateSynchronously(Snapshot, Error));
			TestTrue(TEXT("preflight explains deferred provider validation"), Error.Contains(TEXT("external")));
			TestEqual(TEXT("preflight never calls external provider"), Provider->ValidationCalls.Load(), 0);
			Channel->Source = WorldEditorValidationLocalGraph();
		}
		TestTrue(TEXT("infinite world accepts local control graphs"),
			VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Infinite, Providers,
				WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
		TestFalse(TEXT("unknown world kind is rejected"),
			VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, static_cast<EVoxelLandWorldKind>(255), Providers,
				WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
		TestTrue(TEXT("unknown world kind has an explicit diagnostic"), Error.Contains(TEXT("Unsupported world kind")));
		Snapshot.Mountain.Overrides.AddDefaulted();
		for (const EVoxelLandWorldKind Kind : {EVoxelLandWorldKind::Finite, EVoxelLandWorldKind::Infinite})
		{
			TestFalse(TEXT("unknown override is rejected in both modes"),
				VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, Kind, Providers,
					WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
			TestTrue(TEXT("override diagnostic identifies the control"), Error.Contains(TEXT("Mountain control overrides")));
		}
		Snapshot.Mountain.Overrides.Reset();
		const TCHAR* ChannelNames[] = {TEXT("Continental"), TEXT("Mountain"), TEXT("Peaks")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
		{
			Channels[Index]->Source = WorldEditorValidationNestedGraph();
			TestFalse(TEXT("infinite world rejects nested finite-global requirement in every channel"),
				VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Infinite, Providers,
					WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
			TestTrue(TEXT("diagnostic names affected control"), Error.Contains(ChannelNames[Index]));
			TestTrue(TEXT("diagnostic names containing function"),
				Error.Contains(TEXT("/Game/Test/WorldEditorFunction.WorldEditorFunction")));
			TestTrue(TEXT("diagnostic identifies authored node"),
				Error.Contains(*FGuid(0x7a90, 1, 2, 2).ToString()));
			Channels[Index]->Source = WorldEditorValidationLocalGraph();
		}
		Snapshot.Peaks.CurveKeys[1].InputQ16 = 0;
		TestFalse(TEXT("finite world rejects invalid curve"),
			VoxelLandWorldEditorValidation::ValidateSnapshot(Snapshot, EVoxelLandWorldKind::Finite, Providers,
				WorldEditorValidationBudget().ToSharedRef(), NeverCancel, Error));
		TestTrue(TEXT("diagnostic names invalid curve"), Error.Contains(TEXT("Peaks curve")));
	}
	TestTrue(TEXT("unregister finite-only provider"),
		FVoxelLandOperatorRegistry::Unregister(WorldEditorFiniteProviderId, 1, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldCookGuardTest, "VoxelLand.WorldModes.Editor.CookGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandWorldCookGuardTest::RunTest(const FString& Parameters)
{
	FObjectSaveContextData SaveData;
	SaveData.ObjectSaveContextPhase = EObjectSaveContextPhase::Write;
	FObjectSavePackageSerializeContext SerializeContext(SaveData);
	FArchiveCookContext CookContext(
		nullptr, UE::Cook::ECookType::Unknown, UE::Cook::ECookingDLC::Unknown, nullptr, nullptr);
	FArchiveSavePackageData CookData(SerializeContext, nullptr, &CookContext);
	for (UVoxelLandWorldDefinition* World :
		{static_cast<UVoxelLandWorldDefinition*>(NewObject<UVoxelLandFiniteWorld>()),
			static_cast<UVoxelLandWorldDefinition*>(NewObject<UVoxelLandInfiniteWorld>())})
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Writer.SetSavePackageData(&CookData);
		World->EnforceCookPreparation(Writer);
		TestTrue(TEXT("unfinished world rejects the actual cooker write"), Writer.IsError());
		TArray<uint8> EditorBytes;
		FMemoryWriter EditorWriter(EditorBytes);
		World->EnforceCookPreparation(EditorWriter);
		TestFalse(TEXT("ordinary editor saves remain available"), EditorWriter.IsError());
	}
	return true;
}

#endif
