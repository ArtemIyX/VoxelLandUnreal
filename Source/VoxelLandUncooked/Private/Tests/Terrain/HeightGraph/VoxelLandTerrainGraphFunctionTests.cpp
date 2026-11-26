#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Assets/Graphs/VoxelLandGraphFunction.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4FunctionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandGraphNodeRecord M4FunctionBuiltin(EVoxelLandBuiltinNode Kind)
{
	FVoxelLandBuiltinNodeSpec Spec;
	check(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid::NewGuid();
	Node.OperatorId = Spec.OperatorId;
	Node.OperatorVersion = Spec.OperatorVersion;
	for (const FVoxelLandBuiltinPinSpec& Source : Spec.Pins)
	{
		FVoxelLandGraphPinRecord Pin;
		Pin.Id = FGuid::NewGuid();
		Pin.Name = Source.Name;
		Pin.Type = Source.Type;
		Pin.bOutput = Source.bOutput;
		Node.Pins.Add(Pin);
	}
	return Node;
}

FVoxelLandGraphNodeRecord M4FunctionCall(UVoxelLandGraphFunction& Function)
{
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid::NewGuid();
	Node.OperatorId = VoxelLandGraphFunctionCallId();
	Node.OperatorVersion = 1;
	Node.FunctionPath = FSoftObjectPath(&Function);
	FVoxelLandGraphPinRecord Output;
	Output.Id = FGuid::NewGuid();
	Output.Name = TEXT("Result");
	Output.Type = EVoxelLandGraphValueType::Float;
	Output.bOutput = true;
	Node.Pins.Add(Output);
	return Node;
}

bool M4FunctionBuildFixture(UVoxelLandWorldConfig& Config, UVoxelLandGraphFunction*& Inner,
	UVoxelLandGraphFunction*& Outer, UVoxelLandTerrainGraph*& Root)
{
	Inner = NewObject<UVoxelLandGraphFunction>(&Config);
	Outer = NewObject<UVoxelLandGraphFunction>(&Config);
	Root = NewObject<UVoxelLandTerrainGraph>(&Config);
	Inner->Nodes.Add(M4FunctionBuiltin(EVoxelLandBuiltinNode::PositionX));
	Inner->OutputNode = Inner->Nodes[0].Id;
	Inner->OutputPin = Inner->Nodes[0].Pins[0].Id;
	Outer->Nodes.Add(M4FunctionCall(*Inner));
	Outer->OutputNode = Outer->Nodes[0].Id;
	Outer->OutputPin = Outer->Nodes[0].Pins[0].Id;
	Root->Nodes.Add(M4FunctionCall(*Outer));
	Root->Nodes.Add(M4FunctionBuiltin(EVoxelLandBuiltinNode::DensityOutput));
	const FVoxelLandGraphPinRecord* Input = Root->Nodes[1].Pins.FindByPredicate(
		[](const FVoxelLandGraphPinRecord& Pin) { return !Pin.bOutput && Pin.Name == TEXT("Density"); });
	if (!Input)
	{
		return false;
	}
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Root->Nodes[0].Id;
	Link.SourcePin = Root->Nodes[0].Pins[0].Id;
	Link.TargetNode = Root->Nodes[1].Id;
	Link.TargetPin = Input->Id;
	Root->Links.Add(Link);
	Config.Continental.Graph = Root;
	Config.Mountain.Graph = Root;
	Config.Peaks.Graph = Root;
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	Last.InputQ16 = 65535;
	Config.Continental.CurveKeys = {First, Last};
	Config.Mountain.CurveKeys = {First, Last};
	Config.Peaks.CurveKeys = {First, Last};
	return true;
}

bool M4FunctionCompile2D(const FVoxelLandGraphSnapshot& Source, FString& Error)
{
	return Async(EAsyncExecution::ThreadPool, [&Source, &Error]() {
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandGraphCompiler::Compile(Source, Draft, Error))
		{
			return false;
		}
		if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
		{
			Error = Failure.Message;
			return false;
		}
		if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Program))
		{
			Error = Failure.Message;
			return false;
		}
		return true;
	}).Get();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4FunctionCaptureTest, "VoxelLand.M4.Function.Capture", M4FunctionFlags)
bool FVoxelLandM4FunctionCaptureTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	Config->AddToRoot();
	UVoxelLandGraphFunction* Inner = nullptr;
	UVoxelLandGraphFunction* Outer = nullptr;
	UVoxelLandTerrainGraph* Root = nullptr;
	FString Error;
	FVoxelLandM4WorldSnapshot Snapshot;
	const bool bFixture = M4FunctionBuildFixture(*Config, Inner, Outer, Root);
	const bool bCaptured = bFixture && Config->CaptureSnapshot(Snapshot, Error);
	TestTrue(FString::Printf(TEXT("nested function capture: %s"), *Error), bCaptured);
	if (bCaptured)
	{
		TestEqual(TEXT("both function sources captured"), Snapshot.Continental.Source.Functions.Num(), 2);
		TestTrue(FString::Printf(TEXT("expanded X program is 2D: %s"), *Error),
			M4FunctionCompile2D(Snapshot.Continental.Source, Error));
		Inner->Nodes[0] = M4FunctionBuiltin(EVoxelLandBuiltinNode::PositionZ);
		Inner->OutputNode = Inner->Nodes[0].Id;
		Inner->OutputPin = Inner->Nodes[0].Pins[0].Id;
		FVoxelLandM4WorldSnapshot Changed;
		if (TestTrue(TEXT("changed dependency captures"), Config->CaptureSnapshot(Changed, Error)))
		{
			TestTrue(
				TEXT("original captured source remains 2D"), M4FunctionCompile2D(Snapshot.Continental.Source, Error));
			TestFalse(
				TEXT("expanded Z function is rejected as 2D"), M4FunctionCompile2D(Changed.Continental.Source, Error));
		}
		Outer->Nodes[0].FunctionPath = FSoftObjectPath(Outer);
		TestFalse(TEXT("recursive function rejected"), Config->CaptureSnapshot(Changed, Error));
		TestEqual(
			TEXT("failed capture does not overwrite previous snapshot"), Changed.Continental.Source.Functions.Num(), 2);
	}
	Config->RemoveFromRoot();
	return bCaptured;
}
#endif
