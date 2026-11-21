#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../VoxelLandM4EditorBake.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4CookGuardFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4CookGuardMakeGraph(UVoxelLandTerrainGraph& Graph, FString& Error)
{
	FVoxelLandGraphNodeRecord Constant;
	Constant.Id = FGuid(1, 1, 1, 1);
	Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2);
	Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
	Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
	Constant.Pins[1].bOutput = true;
	FVoxelLandGraphNodeRecord Output;
	Output.Id = FGuid(1, 1, 1, 2);
	Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1);
	Output.Pins[0].Id = FGuid(2, 1, 1, 3);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Constant.Id;
	Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id;
	Link.TargetPin = Output.Pins[0].Id;
	Graph.Nodes = {Constant, Output};
	Graph.Links = {Link};
	FVoxelLandGraphSnapshot Snapshot;
	if (!Graph.CaptureSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandSerializedProgram Payload;
	const bool bPacked = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		return FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error) && !FVoxelLandProgram::Validate(Draft, Program)
			&& FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
	}).Get();
	if (bPacked)
	{
		Graph.CompiledProgram = MoveTemp(Payload);
	}
	return bPacked;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CookGuardTest, "VoxelLand.M4.Editor.CookGuard", M4CookGuardFlags)
bool FVoxelLandM4CookGuardTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	FDataValidationContext EmptyContext;
	TestTrue(
		TEXT("unconfigured asset is invalid"), Config->IsDataValid(EmptyContext) == EDataValidationResult::Invalid);
	TestTrue(TEXT("unconfigured asset reports a validation error"), EmptyContext.GetNumErrors() > 0);

	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	FString Error;
	if (!TestTrue(TEXT("control graph compiles"), M4CookGuardMakeGraph(*Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	Last.InputQ16 = 65535;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	if (!TestTrue(TEXT("valid asset bakes"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	FDataValidationContext ValidContext;
	TestTrue(
		TEXT("saved settings and LUTs validate"), Config->IsDataValid(ValidContext) == EDataValidationResult::Valid);

	const FVoxelLandSerializedProgram GoodPayload = Graph->CompiledProgram;
	Graph->CompiledProgram.EnvelopeVersion = 99;
	FDataValidationContext InvalidPayloadContext;
	TestTrue(TEXT("asset validation rejects unsupported payload even with existing LUTs"),
		Config->IsDataValid(InvalidPayloadContext) == EDataValidationResult::Invalid);
	TestFalse(TEXT("unsupported compiled control cannot bake"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error));
	TestTrue(TEXT("failed worker validation reports an error"), !Error.IsEmpty());
	TestFalse(TEXT("failed bake clears old LUTs"), Config->ValidateBakedCurves(Error));

	Graph->CompiledProgram = GoodPayload;
	if (!TestTrue(TEXT("restored graph bakes"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		return false;
	}
	Graph->Nodes[0].Pins[0].FloatDefault = 1.0f;
	TestFalse(TEXT("source edit rejects stale compiled graph"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error));
	TestFalse(TEXT("stale source clears old LUTs"), Config->ValidateBakedCurves(Error));
	Graph->Nodes[0].Pins[0].FloatDefault = 0.0f;
	if (!TestTrue(TEXT("restored graph bakes again"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		return false;
	}
	Graph->Nodes[0].Pins[0].FloatDefault = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("invalid graph cannot be recaptured"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error));
	TestFalse(TEXT("capture failure also clears old LUTs"), Config->ValidateBakedCurves(Error));
	return true;
}

#endif
