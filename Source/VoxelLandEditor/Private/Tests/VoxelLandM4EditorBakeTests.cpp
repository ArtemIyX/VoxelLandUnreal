#if WITH_DEV_AUTOMATION_TESTS

#include "../VoxelLandAssetFactories.h"
#include "../VoxelLandM4EditorBake.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4Preparation.h"

namespace
{
constexpr EAutomationTestFlags M4EditorBakeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4EditorBakeMakeGraph(UVoxelLandTerrainGraph& Graph, FString& Error)
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
	const bool bOk = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		return FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error) && !FVoxelLandProgram::Validate(Draft, Program)
			&& FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
	}).Get();
	if (bOk)
	{
		Graph.CompiledProgram = MoveTemp(Payload);
	}
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4EditorBakeTest, "VoxelLand.M4.Editor.Bake", M4EditorBakeFlags)
bool FVoxelLandM4EditorBakeTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfigFactory* Factory = NewObject<UVoxelLandM4WorldConfigFactory>();
	UVoxelLandM4WorldConfig* Config = Cast<UVoxelLandM4WorldConfig>(Factory->FactoryCreateNew(
		UVoxelLandM4WorldConfig::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, nullptr));
	if (!TestNotNull(TEXT("factory creates an M4 configuration"), Config))
	{
		return false;
	}
	TestTrue(TEXT("new configuration is transactional"), Config->HasAnyFlags(RF_Transactional));
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	FString Error;
	if (!TestTrue(TEXT("control graph compiles"), M4EditorBakeMakeGraph(*Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	First.OutputQ8 = -256;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = 256;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	if (!TestTrue(TEXT("valid controls bake"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("baked curves match authored keys"), Config->ValidateBakedCurves(Error));
	Config->Mountain.CurveKeys[1].InputQ16 = 0;
	TestFalse(TEXT("invalid edited curve cannot bake"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error));
	TestFalse(TEXT("stale curves cannot validate after key edit"), Config->ValidateBakedCurves(Error));
	return true;
}

namespace
{
class FM4EditorPreviewSafetyCommand final : public IAutomationLatentCommand
{
public:
	FM4EditorPreviewSafetyCommand(
		FAutomationTestBase* InTest, UVoxelLandM4WorldConfig* InConfig, TSharedRef<FVoxelLandGraphToolkit> InToolkit)
		: Test(InTest)
		, Config(InConfig)
		, Toolkit(InToolkit)
		, Started(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		if (!IsValid(Config) || FPlatformTime::Seconds() - Started > 30.0)
		{
			Test->AddError(TEXT("M4 preview did not complete"));
			Cleanup();
			return true;
		}
		Toolkit->PollJobsForTests();
		if (Phase == 0)
		{
			if (Toolkit->HasM4PreviewWorkForTests())
			{
				return false;
			}
			if (!Test->TestEqual(TEXT("initial M4 preview publishes"), Toolkit->GetPreviewPublishCountForTests(), 1))
			{
				Test->AddError(Toolkit->GetStatusForTests());
				Cleanup();
				return true;
			}
			Test->TestTrue(TEXT("inspector reports the completed global height grid"),
				Toolkit->GetM4StageInspectorForTests().Contains(TEXT("stored height range")));
			++Config->Seed;
			Toolkit->StartM4PreviewForTests(Config);
			Test->TestTrue(TEXT("previous preview is visibly stale while replacement runs"),
				Toolkit->GetStatusForTests().Contains(TEXT("stale"), ESearchCase::IgnoreCase));
			Test->TestTrue(TEXT("earlier inspector values are marked stale during replacement"),
				Toolkit->GetM4StageInspectorForTests().Contains(TEXT("stale"), ESearchCase::IgnoreCase));
			// No property notification is needed for the publication-time guard:
			// change source immediately after capturing it but before pumping completion.
			++Config->Seed;
			Phase = 1;
			return false;
		}
		if (Toolkit->HasM4PreviewWorkForTests())
		{
			return false;
		}
		Test->TestEqual(
			TEXT("superseded result never replaces old preview"), Toolkit->GetPreviewPublishCountForTests(), 1);
		Test->TestTrue(TEXT("superseded result reports stale"),
			Toolkit->GetStatusForTests().Contains(TEXT("stale"), ESearchCase::IgnoreCase));
		Cleanup();
		return true;
	}

private:
	void Cleanup()
	{
		Toolkit->Stop();
		if (IsValid(Config))
		{
			Config->RemoveFromRoot();
		}
	}
	FAutomationTestBase* Test;
	UVoxelLandM4WorldConfig* Config;
	TSharedRef<FVoxelLandGraphToolkit> Toolkit;
	double Started;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4EditorPreviewSafetyTest, "VoxelLand.M4.Editor.PreviewSafety", M4EditorBakeFlags)
bool FVoxelLandM4EditorPreviewSafetyTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->AddToRoot();
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	FString Error;
	if (!M4EditorBakeMakeGraph(*Graph, Error))
	{
		AddError(Error);
		Config->RemoveFromRoot();
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	Last.InputQ16 = 65535;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->CellsX = Config->CellsY = 4;
	Config->OriginXCm = Config->OriginYCm = -12800;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	if (!VoxelLandM4Editor::BakeWorldConfig(*Config, Error))
	{
		AddError(Error);
		Config->RemoveFromRoot();
		return false;
	}
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config);
	ADD_LATENT_AUTOMATION_COMMAND(FM4EditorPreviewSafetyCommand(this, Config, Toolkit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4EditorValidationAdmissionTest, "VoxelLand.M4.Editor.ValidationAdmission", M4EditorBakeFlags)
bool FVoxelLandM4EditorValidationAdmissionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("shared coordinator is available"), VoxelLandM4Preparation::EnsureCoordinatorReady()))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>> Permits;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto Permit = VoxelLandM4Preparation::ReserveValidation();
		if (!TestTrue(TEXT("validation permit is available"), Permit.IsValid()))
		{
			return false;
		}
		Permits.Add(MoveTemp(Permit));
	}
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config);
	TestTrue(TEXT("preview reports exhausted validation admission"),
		Toolkit->GetStatusForTests().Contains(TEXT("snapshot validation capacity")));
	TestFalse(TEXT("rejected preview does not launch work"), Toolkit->HasM4PreviewWorkForTests());
	Permits[0].Reset();
	TestTrue(TEXT("released permit can be acquired again"), VoxelLandM4Preparation::ReserveValidation().IsValid());
	Toolkit->Stop();
	return true;
}

#endif
