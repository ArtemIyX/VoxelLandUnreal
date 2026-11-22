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
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4Preparation.h"

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
#endif
