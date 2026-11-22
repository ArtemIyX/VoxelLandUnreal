#if WITH_DEV_AUTOMATION_TESTS

#include "../VoxelLandAssetFactories.h"
#include "../VoxelLandM4EditorBake.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4ControlGraph.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandEditorNode.h"
#include "EdGraph/EdGraph.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4Preparation.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/VoxelLandM4HeightStageValidation.h"

namespace
{
constexpr EAutomationTestFlags M4NamedGraphFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

void M4NamedAppendChannel(UVoxelLandM4ControlGraph& Graph, int32 Index, float Value, FGuid& OutputId)
{
	FVoxelLandGraphNodeRecord Constant;
	Constant.Id = FGuid(100, 0, 0, Index + 1);
	Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2);
	Constant.Pins[0].Id = FGuid(200, 0, Index + 1, 1);
	Constant.Pins[0].FloatDefault = Value;
	Constant.Pins[1].Id = FGuid(200, 0, Index + 1, 2);
	Constant.Pins[1].bOutput = true;
	FVoxelLandGraphNodeRecord Output;
	Output.Id = FGuid(300, 0, 0, Index + 1);
	OutputId = Output.Id;
	Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1);
	Output.Pins[0].Id = FGuid(400, 0, 0, Index + 1);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Constant.Id;
	Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id;
	Link.TargetPin = Output.Pins[0].Id;
	Graph.Nodes.Add(Constant);
	Graph.Nodes.Add(Output);
	Graph.Links.Add(Link);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4NamedGraphTest, "VoxelLand.M4.Editor.NamedControlGraph", M4NamedGraphFlags)
bool FVoxelLandM4NamedGraphTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraphFactory* Factory = NewObject<UVoxelLandM4ControlGraphFactory>();
	UVoxelLandM4ControlGraph* Graph = Cast<UVoxelLandM4ControlGraph>(Factory->FactoryCreateNew(
		UVoxelLandM4ControlGraph::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, nullptr));
	if (!TestNotNull(TEXT("named graph factory creates asset"), Graph))
	{
		return false;
	}
	TestTrue(TEXT("named graph is transactional"), Graph->HasAnyFlags(RF_Transactional));
	M4NamedAppendChannel(*Graph, 0, -1.f, Graph->ContinentalOutputId);
	M4NamedAppendChannel(*Graph, 1, 0.f, Graph->MountainOutputId);
	M4NamedAppendChannel(*Graph, 2, 1.f, Graph->PeaksOutputId);
	FString Error;
	if (!TestTrue(TEXT("three named outputs compile"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	Last.InputQ16 = 65535;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->CellsX = Config->CellsY = 8;
	Config->OriginXCm = Config->OriginYCm = -25600;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	if (!TestTrue(TEXT("shared named graph and curves bake"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!TestTrue(TEXT("named graph captures projected programs"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	const auto Lease = VoxelLandM4Preparation::Reserve();
	if (!TestTrue(TEXT("prepass admission available"), Lease.IsValid()))
	{
		return false;
	}
	TArray<uint16> Samples[3];
	bool bPrepared = false;
	const bool bEvaluated = Async(EAsyncExecution::ThreadPool, [&] {
		const FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandProgramCodec::Load(Controls[Index]->Source, Controls[Index]->Payload, Program, Error))
			{
				return false;
			}
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (FVoxelLandGenerator::Create(Settings, Generator) || Generator->CreateWorker(Worker)
				|| VoxelLandM4ControlGrid::Evaluate(
					*Generator, *Worker, 0, 0, 1, 1, 1, Samples[Index], [] { return false; }))
			{
				return false;
			}
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Final;
		bPrepared = VoxelLandM4Preparation::Prepare(Snapshot, Lease, [] { return false; }, Final, Error);
		return true;
	}).Get();
	TestTrue(TEXT("all projected programs evaluate"), bEvaluated);
	if (bEvaluated)
	{
		TestTrue(TEXT("continental output is -1"), Samples[0] == TArray<uint16>({0, 0, 0, 0}));
		TestTrue(TEXT("mountain output is 0"), Samples[1] == TArray<uint16>({32768, 32768, 32768, 32768}));
		TestTrue(TEXT("peaks output is +1"), Samples[2] == TArray<uint16>({65535, 65535, 65535, 65535}));
		TestTrue(TEXT("runtime prepares the projected named channels"), bPrepared);
	}
	// The mountain terminal may share the continental upstream subgraph.
	const FVoxelLandGraphLinkRecord MountainLink = Graph->Links[1];
	Graph->Links[1].SourceNode = Graph->Nodes[0].Id;
	Graph->Links[1].SourcePin = Graph->Nodes[0].Pins[1].Id;
	TestTrue(TEXT("shared upstream compiles"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error));
	FVoxelLandM4WorldSnapshot Shared;
	TestTrue(TEXT("shared upstream captures"), Config->CaptureSnapshot(Shared, Error));
	TArray<uint16> SharedSamples;
	const bool bSharedEvaluated = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandProgramCodec::Load(Shared.Mountain.Source, Shared.Mountain.Payload, Program, Error))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		return !FVoxelLandGenerator::Create(Settings, Generator) && !Generator->CreateWorker(Worker)
			&& !VoxelLandM4ControlGrid::Evaluate(
				*Generator, *Worker, 0, 0, 1, 1, 1, SharedSamples, [] { return false; });
	}).Get();
	TestTrue(TEXT("shared upstream gives mountain continental value"),
		bSharedEvaluated && SharedSamples == TArray<uint16>({0, 0, 0, 0}));
	Graph->Links[1] = MountainLink;
	TestTrue(TEXT("restored graph compiles"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error));
	Graph->Nodes[0].Pins[0].FloatDefault = -0.5f;
	FVoxelLandM4WorldSnapshot Edited;
	TestTrue(TEXT("edited named graph captures"), Config->CaptureSnapshot(Edited, Error));
	TestTrue(TEXT("named source edit changes projected source identity"),
		FVoxelLandProgramCodec::SourceFingerprint(Edited.Continental.Source)
			!= FVoxelLandProgramCodec::SourceFingerprint(Snapshot.Continental.Source));
	const bool bStaleRejected = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		return !FVoxelLandProgramCodec::Load(Edited.Continental.Source, Edited.Continental.Payload, Program, Error);
	}).Get();
	TestTrue(TEXT("source edit rejects old cooked program"), bStaleRejected);
	TestTrue(TEXT("changed graph recompiles"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error));
	FVoxelLandGraphNodeRecord& PeaksSource = Graph->Nodes[4];
	PeaksSource.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionZ);
	PeaksSource.Pins.RemoveAt(0);
	TestFalse(TEXT("3D dependency is rejected"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error));
	TestTrue(TEXT("3D rejection clears all named programs"), Graph->PeaksProgram.Instructions.IsEmpty());
	Graph->MountainOutputId = Graph->ContinentalOutputId;
	TestFalse(TEXT("duplicate named output rejected"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error));
	TestTrue(TEXT("failed compilation clears old named programs"), Graph->ContinentalProgram.Instructions.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4HeightOutputTest, "VoxelLand.M4.Editor.HeightOutput", M4NamedGraphFlags)
bool FVoxelLandM4HeightOutputTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	FGuid Unused[3];
	M4NamedAppendChannel(*Graph, 0, -1.f, Unused[0]);
	M4NamedAppendChannel(*Graph, 1, 0.f, Unused[1]);
	M4NamedAppendChannel(*Graph, 2, 1.f, Unused[2]);
	Graph->Nodes.RemoveAll([](const FVoxelLandGraphNodeRecord& Node) {
		return Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	});
	FVoxelLandGraphNodeRecord Height;
	Height.Id = FGuid(500, 0, 0, 1);
	Height.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::M4HeightOutput);
	Height.Pins.SetNum(3);
	for (int32 I = 0; I < 3; ++I)
	{
		Height.Pins[I].Id = FGuid(501, 0, 0, I + 1);
		Graph->Links[I].TargetNode = Height.Id;
		Graph->Links[I].TargetPin = Height.Pins[I].Id;
	}
	Graph->HeightOutputId = Height.Id;
	Graph->Nodes.Add(Height);
	FString Error;
	if (!TestTrue(TEXT("three-input height output compiles"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = 25600;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->CellsX = Config->CellsY = 8;
	Config->OriginXCm = Config->OriginYCm = -25600;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	if (!TestTrue(TEXT("height-stage config bakes"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!TestTrue(TEXT("height-stage snapshot captures"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("height-stage manifest version"), Snapshot.HeightStage.Version, 1);
	TestTrue(TEXT("height-stage manifest references output"), Snapshot.HeightStage.OutputNodeId == Height.Id);
	TestTrue(TEXT("height-stage manifest matches its projections"),
		VoxelLandM4HeightStageValidation::Validate(Snapshot, Error));
	TestTrue(TEXT("authored config matches stage snapshot"), Config->MatchesSnapshot(Snapshot));
	FVoxelLandM4WorldSnapshot Broken = Snapshot;
	Broken.HeightStage.OutputNodeId = FGuid(2, 3, 4, 5);
	TestFalse(TEXT("tampered output identity rejected"), VoxelLandM4HeightStageValidation::Validate(Broken, Error));
	Broken = Snapshot;
	Broken.Mountain.Source.Nodes.Last().Pins[0].Id = Broken.Continental.Source.Nodes.Last().Pins[0].Id;
	TestFalse(TEXT("duplicate projected inputs rejected"), VoxelLandM4HeightStageValidation::Validate(Broken, Error));
	auto Lease = VoxelLandM4Preparation::Reserve();
	if (!TestTrue(TEXT("finite admission available"), Lease.IsValid()))
	{
		return false;
	}
	FVoxelLandM4IdentityRecord Identity;
	bool bUnknownVersionRejected = false;
	int16 FiniteDensity = 0;
	const bool bFinite = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		FVoxelLandM4IdentityRecord Old;
		bUnknownVersionRejected = !VoxelLandM4WorldIdentity::CaptureVersion(Snapshot, 2, Old, Error);
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		FVoxelLandChunkKey Key;
		if (!VoxelLandM4WorldIdentity::Capture(Snapshot, Identity, Error)
			|| !VoxelLandM4WorldIdentity::Matches(Identity, Snapshot, Error)
			|| !VoxelLandM4Preparation::Prepare(
				Snapshot, Lease, [] { return false; }, Generator, Error)
			|| Generator->CreateWorker(Worker) || Generator->GenerateChunk(Key, *Worker, Chunk) || !Chunk)
		{
			return false;
		}
		FiniteDensity = Chunk->Density[1 + 35 * (1 + 35)];
		return true;
	}).Get();
	TestTrue(TEXT("height output runs in finite mode"), bFinite);
	TestEqual(TEXT("height combines all three graph inputs through curves"), int32(FiniteDensity), -1536);
	TestTrue(TEXT("unsupported identity version is rejected"), bUnknownVersionRejected);
	TestEqual(TEXT("height stage uses current world identity"), Identity.FormatVersion, 1);
	Lease.Reset();
	Config->bUnlimited = true;
	if (!TestTrue(TEXT("height output captures unlimited mode"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		return false;
	}
	Lease = VoxelLandM4Preparation::Reserve();
	if (!TestTrue(TEXT("unlimited admission available"), Lease.IsValid()))
	{
		return false;
	}
	int16 UnlimitedDensity = 0;
	const bool bUnlimited = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		FVoxelLandChunkKey Key;
		if (!VoxelLandM4Preparation::Prepare(
				Snapshot, Lease, [] { return false; }, Generator, Error)
			|| Generator->CreateWorker(Worker) || Generator->GenerateChunk(Key, *Worker, Chunk) || !Chunk)
		{
			return false;
		}
		UnlimitedDensity = Chunk->Density[1 + 35 * (1 + 35)];
		return true;
	}).Get();
	TestTrue(TEXT("height output generates unlimited chunk"), bUnlimited);
	TestEqual(TEXT("unlimited height uses the same graph combination"), int32(UnlimitedDensity), -1536);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4LegacyOutputAssignmentTest, "VoxelLand.M4.Editor.LegacyOutputAssignment", M4NamedGraphFlags)
bool FVoxelLandM4LegacyOutputAssignmentTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>();
	M4NamedAppendChannel(*Graph, 0, -1.f, Graph->ContinentalOutputId);
	M4NamedAppendChannel(*Graph, 1, 0.f, Graph->MountainOutputId);
	M4NamedAppendChannel(*Graph, 2, 1.f, Graph->PeaksOutputId);
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	UEdGraph* EditorGraph = Graph->EditorGraph.Get();
	if (!TestNotNull(TEXT("legacy editor graph exists"), EditorGraph))
	{
		Toolkit->Stop();
		return false;
	}
	FString Error;
	if (!TestTrue(TEXT("baseline outputs compile"), VoxelLandM4Editor::CompileNamedGraph(*Graph, Error)))
	{
		AddError(Error);
		Toolkit->Stop();
		return false;
	}
	UVoxelLandEditorNode* Output = nullptr;
	for (UEdGraphNode* Node : EditorGraph->Nodes)
	{
		if (Node && Node->NodeGuid == Graph->ContinentalOutputId)
		{
			Output = Cast<UVoxelLandEditorNode>(Node);
			break;
		}
	}
	if (!TestNotNull(TEXT("continental output exists"), Output))
	{
		Toolkit->Stop();
		return false;
	}
	const uint64 Before = Toolkit->GetRevisionForTests();
	Output->NodePosX += 10;
	EditorGraph->NotifyGraphChanged();
	TestEqual(TEXT("layout edit preserves graph revision"), Toolkit->GetRevisionForTests(), Before);
	TestFalse(TEXT("layout edit does not schedule compile"), Toolkit->HasScheduledCompileForTests());
	TestTrue(TEXT("assignment uses existing control terminal"),
		UVoxelLandEditorSchema::AssignLegacyM4Output(*Graph, *EditorGraph, Output->NodeGuid, 1));
	TestTrue(TEXT("assignment advances revision"), Toolkit->GetRevisionForTests() > Before);
	TestTrue(TEXT("assignment schedules asynchronous compilation"), Toolkit->HasScheduledCompileForTests());
	TestFalse(TEXT("assignment does not synchronously start worker"), Toolkit->HasCompileWorkForTests());
	FVoxelLandGraphSnapshot Source, Projection;
	TestTrue(TEXT("source still captures"), Graph->CaptureSnapshot(Source, Error));
	TestFalse(TEXT("incomplete output assignment rejects prior programs"),
		Graph->ProjectSnapshot(Source, EVoxelLandM4ControlChannel::Mountain, Projection, Error));
	TestFalse(TEXT("invalid channel assignment rejected"),
		UVoxelLandEditorSchema::AssignLegacyM4Output(*Graph, *EditorGraph, Output->NodeGuid, 3));
	Toolkit->Stop();
	return true;
}

#endif
