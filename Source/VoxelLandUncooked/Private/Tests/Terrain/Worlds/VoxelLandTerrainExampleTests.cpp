#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveBake.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"

namespace
{
constexpr EAutomationTestFlags M4ExampleFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4ExampleAddNode(
	UVoxelLandTerrainGraph& Graph, int32 Channel, int32 Ordinal, EVoxelLandBuiltinNode Kind, FString& Error)
{
	FVoxelLandBuiltinNodeSpec Spec;
	if (!FVoxelLandBuiltinCatalog::Find(Kind, Spec))
	{
		Error = TEXT("Example builtin is unavailable");
		return false;
	}
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(0x4d344558, uint32(Channel + 1), 1, uint32(Ordinal + 1));
	Node.OperatorId = Spec.OperatorId;
	Node.OperatorVersion = Spec.OperatorVersion;
	Node.SeedSalt = 17 + Channel * 29;
	for (int32 Index = 0; Index < Spec.Pins.Num(); ++Index)
	{
		const FVoxelLandBuiltinPinSpec& Source = Spec.Pins[Index];
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = FGuid(0x4d345049, uint32(Channel + 1), uint32(Ordinal + 1), uint32(Index + 1));
		Pin.Name = Source.Name;
		Pin.Type = Source.Type;
		Pin.bOutput = Source.bOutput;
		if (!Pin.bOutput)
		{
			if (Pin.Type == EVoxelLandGraphValueType::Float)
			{
				Pin.FloatDefault = FCString::Atof(*Source.DefaultLiteral);
			}
			else if (Pin.Type == EVoxelLandGraphValueType::Int32)
			{
				Pin.IntDefault = FCString::Atoi(*Source.DefaultLiteral);
			}
			else
			{
				Pin.BoolDefault = Source.DefaultLiteral.Equals(TEXT("true"), ESearchCase::IgnoreCase);
			}
		}
		Node.Pins.Add(MoveTemp(Pin));
	}
	Graph.Nodes.Add(MoveTemp(Node));
	return true;
}

bool M4ExampleWire(UVoxelLandTerrainGraph& Graph, int32 From, int32 To, const TCHAR* TargetName, FString& Error)
{
	const FVoxelLandGraphNodeRecord& Source = Graph.Nodes[From];
	const FVoxelLandGraphNodeRecord& Target = Graph.Nodes[To];
	const FVoxelLandGraphPinRecord* Output =
		Source.Pins.FindByPredicate([](const FVoxelLandGraphPinRecord& P) { return P.bOutput; });
	const FVoxelLandGraphPinRecord* Input = Target.Pins.FindByPredicate(
		[TargetName](const FVoxelLandGraphPinRecord& P) { return !P.bOutput && P.Name == TargetName; });
	if (!Output || !Input)
	{
		Error = TEXT("Example graph pin is unavailable");
		return false;
	}
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Source.Id;
	Link.SourcePin = Output->Id;
	Link.TargetNode = Target.Id;
	Link.TargetPin = Input->Id;
	Graph.Links.Add(Link);
	return true;
}

bool M4ExampleGraph(UVoxelLandTerrainGraph& Graph, int32 Channel, float Frequency, FString& Error)
{
	if (!M4ExampleAddNode(Graph, Channel, 0, EVoxelLandBuiltinNode::PositionX, Error)
		|| !M4ExampleAddNode(Graph, Channel, 1, EVoxelLandBuiltinNode::PositionY, Error)
		|| !M4ExampleAddNode(Graph, Channel, 2, EVoxelLandBuiltinNode::Fbm2, Error)
		|| !M4ExampleAddNode(Graph, Channel, 3, EVoxelLandBuiltinNode::DensityOutput, Error))
	{
		return false;
	}
	FVoxelLandGraphPinRecord* FrequencyPin = Graph.Nodes[2].Pins.FindByPredicate(
		[](const FVoxelLandGraphPinRecord& P) { return !P.bOutput && P.Name == TEXT("Frequency"); });
	if (!FrequencyPin)
	{
		Error = TEXT("Example noise frequency pin is unavailable");
		return false;
	}
	FrequencyPin->FloatDefault = Frequency;
	return M4ExampleWire(Graph, 0, 2, TEXT("X"), Error) && M4ExampleWire(Graph, 1, 2, TEXT("Y"), Error)
		&& M4ExampleWire(Graph, 2, 3, TEXT("Density"), Error);
}

void M4ExampleCurve(FVoxelLandM4ControlConfig& Channel, int32 StartQ8, int32 MiddleQ8, int32 EndQ8)
{
	FVoxelLandM4CurveAuthoringKey First, Middle, Last;
	First.InputQ16 = 0;
	First.OutputQ8 = StartQ8;
	Middle.InputQ16 = 32768;
	Middle.OutputQ8 = MiddleQ8;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = EndQ8;
	Channel.CurveKeys = {First, Middle, Last};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ExampleReproducibility, "VoxelLand.M4.Example.Reproducibility", M4ExampleFlags)
bool FVoxelLandM4ExampleReproducibility::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->AddToRoot();
	UVoxelLandTerrainGraph* Graphs[3] = {NewObject<UVoxelLandTerrainGraph>(Config),
		NewObject<UVoxelLandTerrainGraph>(Config), NewObject<UVoxelLandTerrainGraph>(Config)};
	const float Frequencies[] = {0.00002f, 0.00006f, 0.00012f};
	FString Error;
	FVoxelLandGraphSnapshot Sources[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!M4ExampleGraph(*Graphs[Index], Index, Frequencies[Index], Error)
			|| !Graphs[Index]->CaptureSnapshot(Sources[Index], Error))
		{
			AddError(FString::Printf(TEXT("Could not capture example control %d: %s"), Index, *Error));
			Config->RemoveFromRoot();
			return false;
		}
	}
	Config->Continental.Graph = Graphs[0];
	Config->Mountain.Graph = Graphs[1];
	Config->Peaks.Graph = Graphs[2];
	M4ExampleCurve(Config->Continental, -12000 * 256, 0, 24000 * 256);
	M4ExampleCurve(Config->Mountain, 0, 2000 * 256, 14000 * 256);
	M4ExampleCurve(Config->Peaks, -3000 * 256, 0, 6000 * 256);
	Config->Seed = 271828;
	Config->VoxelSizeCm = 100;
	Config->CellsX = Config->CellsY = 8;
	Config->OriginXCm = Config->OriginYCm = -25600;
	Config->GridSpacingCm = 6400;
	Config->DropletCount = 16;
	Config->MaxSteps = 8;
	Config->ThermalIterations = 1;
	Config->bGlobalErosion = true;
	Config->bOverhangEnabled = true;
	Config->OverhangAmplitudeQ8 = 64 * 256;
	Config->OverhangBandQ8 = 1000 * 256;
	Config->OverhangFrequencyPerCm = 0.001f;
	TArray<FVoxelLandM4CurveKey> CurveKeys[3];
	const FVoxelLandM4ControlConfig* Channels[] = {&Config->Continental, &Config->Mountain, &Config->Peaks};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		for (const FVoxelLandM4CurveAuthoringKey& Source : Channels[Index]->CurveKeys)
		{
			FVoxelLandM4CurveKey Key;
			Key.InputQ16 = uint16(Source.InputQ16);
			Key.Output = Source.OutputQ8;
			CurveKeys[Index].Add(Key);
		}
	}
	FVoxelLandSerializedProgram Payloads[3];
	TArray<int32> Curves[3];
	const bool bBuilt = Async(EAsyncExecution::ThreadPool, [&]() {
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(Sources[Index], Draft, Error))
			{
				return false;
			}
			if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
			{
				Error = Failure.Message;
				return false;
			}
			if (!FVoxelLandProgramCodec::Pack(Sources[Index], *Program, Payloads[Index], Error))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
			if (!VoxelLandM4CurveBake::Bake(CurveKeys[Index], -512000000, 512000000, Curves[Index], Curve, Error))
			{
				return false;
			}
		}
		return true;
	}).Get();
	if (!bBuilt)
	{
		AddError(FString::Printf(TEXT("Example compilation failed: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Graphs[Index]->CompiledProgram = MoveTemp(Payloads[Index]);
	}
	if (!Config->SetBakedCurves(Curves[0], Curves[1], Curves[2], int32(VoxelLandM4CurveBake::AlgorithmVersion), Error)
		|| !Config->ValidateBakedCurves(Error))
	{
		AddError(FString::Printf(TEXT("Example curve preparation failed: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!Config->CaptureSnapshot(Snapshot, Error))
	{
		AddError(Error);
		Config->RemoveFromRoot();
		return false;
	}
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease = VoxelLandM4Preparation::Reserve();
	if (!Lease)
	{
		AddError(TEXT("Example could not reserve M4 prepass budget"));
		Config->RemoveFromRoot();
		return false;
	}
	uint64 HashA = 0, HashB = 0, RepeatA = 0, RepeatB = 0;
	const bool bGenerated = Async(EAsyncExecution::ThreadPool, [&]() {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (!VoxelLandM4Preparation::Prepare(Snapshot, Lease, [] { return false; }, Generator, Error))
		{
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Generator->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		FVoxelLandChunkKey A, B;
		B.Coordinate.X = 1;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		if (const FVoxelLandError Failure = Generator->GenerateChunk(A, *Worker, Chunk))
		{
			Error = Failure.Message;
			return false;
		}
		HashA = Chunk->Hash;
		if (const FVoxelLandError Failure = Generator->GenerateChunk(B, *Worker, Chunk))
		{
			Error = Failure.Message;
			return false;
		}
		HashB = Chunk->Hash;
		if (const FVoxelLandError Failure = Generator->GenerateChunk(B, *Worker, Chunk))
		{
			Error = Failure.Message;
			return false;
		}
		RepeatB = Chunk->Hash;
		if (const FVoxelLandError Failure = Generator->GenerateChunk(A, *Worker, Chunk))
		{
			Error = Failure.Message;
			return false;
		}
		RepeatA = Chunk->Hash;
		return true;
	}).Get();
	if (!bGenerated)
	{
		AddError(FString::Printf(TEXT("Example generation failed: %s"), *Error));
	}
	else
	{
		TestEqual(TEXT("first chunk stable after reverse request"), HashA, RepeatA);
		TestEqual(TEXT("second chunk stable after reverse request"), HashB, RepeatB);
		TestTrue(TEXT("three control graphs are independently identified"),
			Sources[0].Nodes[2].Id != Sources[1].Nodes[2].Id && Sources[1].Nodes[2].Id != Sources[2].Nodes[2].Id);
	}
	Config->RemoveFromRoot();
	return bGenerated;
}
#endif
