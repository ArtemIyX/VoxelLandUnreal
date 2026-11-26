#if WITH_DEV_AUTOMATION_TESTS

#include "../../../Worlds/Assets/VoxelLandAssetFactories.h"
#include "../../../VoxelLandEditorBake.h"
#include "Async/Async.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "../../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "../../../Graph/Schema/VoxelLandEditorNode.h"
#include "EdGraph/EdGraph.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"

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
	UVoxelLandWorldConfigFactory* Factory = NewObject<UVoxelLandWorldConfigFactory>();
	UVoxelLandWorldConfig* Config = Cast<UVoxelLandWorldConfig>(Factory->FactoryCreateNew(
		UVoxelLandWorldConfig::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, nullptr));
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
		FAutomationTestBase* InTest, UVoxelLandWorldConfig* InConfig, TSharedRef<FVoxelLandGraphToolkit> InToolkit)
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
	UVoxelLandWorldConfig* Config;
	TSharedRef<FVoxelLandGraphToolkit> Toolkit;
	double Started;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4EditorPreviewSafetyTest, "VoxelLand.M4.Editor.PreviewSafety", M4EditorBakeFlags)
bool FVoxelLandM4EditorPreviewSafetyTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4EditorUndoTest, "VoxelLand.M4.Editor.PreviewUndo", M4EditorBakeFlags)
bool FVoxelLandM4EditorUndoTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("editor is available"), GEditor))
	{
		return false;
	}
	UVoxelLandWorldConfig* Config =
		NewObject<UVoxelLandWorldConfig>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config, NAME_None, RF_Transactional);
	FString Error;
	if (!M4EditorBakeMakeGraph(*Graph, Error))
	{
		AddError(Error);
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	FVoxelLandM4CurveAuthoringKey First, Last;
	Last.InputQ16 = 65535;
	Config->Continental.CurveKeys = Config->Mountain.CurveKeys = Config->Peaks.CurveKeys = {First, Last};
	Config->CellsX = Config->CellsY = 4;
	Config->OriginXCm = Config->OriginYCm = -12800;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	if (!VoxelLandM4Editor::BakeWorldConfig(*Config, Error))
	{
		AddError(Error);
		return false;
	}
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config);
	const uint64 BeforeMaterial = Toolkit->GetPreviewRevisionForTests();
	const int32 BeforeApply = Toolkit->GetMaterialApplyCountForTests();
	UVoxelLandMaterialSetup* Setup =
		NewObject<UVoxelLandMaterialSetup>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("VoxelLandTests", "M4PreviewMaterialUndo", "M4 preview material"));
		Config->Modify();
		Config->MaterialSetup = Setup;
	}
	TestTrue(TEXT("M4 material undo succeeds"), GEditor->UndoTransaction());
	TestEqual(TEXT("M4 material undo does not regenerate"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
	TestTrue(TEXT("M4 material undo reapplies presentation"), Toolkit->GetMaterialApplyCountForTests() > BeforeApply);
	const int32 BeforeRedoApply = Toolkit->GetMaterialApplyCountForTests();
	TestTrue(TEXT("M4 material redo succeeds"), GEditor->RedoTransaction());
	TestEqual(TEXT("M4 material redo does not regenerate"), Toolkit->GetPreviewRevisionForTests(), BeforeMaterial);
	TestTrue(
		TEXT("M4 material redo reapplies presentation"), Toolkit->GetMaterialApplyCountForTests() > BeforeRedoApply);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "M4PreviewSeedUndo", "M4 preview seed"));
		Config->Modify();
		++Config->Seed;
	}
	const uint64 BeforeSeed = Toolkit->GetPreviewRevisionForTests();
	TestTrue(TEXT("M4 seed undo succeeds"), GEditor->UndoTransaction());
	TestTrue(TEXT("M4 seed undo invalidates old preview"), Toolkit->GetPreviewRevisionForTests() > BeforeSeed);
	const uint64 BeforeSeedRedo = Toolkit->GetPreviewRevisionForTests();
	TestTrue(TEXT("M4 seed redo succeeds"), GEditor->RedoTransaction());
	TestTrue(TEXT("M4 seed redo invalidates old preview"), Toolkit->GetPreviewRevisionForTests() > BeforeSeedRedo);
	Toolkit->Stop();
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
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4InspectionReportLifecycleTest, "VoxelLand.M4.Editor.InspectionReportLifecycle", M4EditorBakeFlags)
bool FVoxelLandM4InspectionReportLifecycleTest::RunTest(const FString& Parameters)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>();
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	Toolkit->SetM4InspectionDetailsForTests(TEXT("old report"));
	Toolkit->BeginPIEForTests();
	TestTrue(TEXT("entering PIE invalidates the report"), Toolkit->GetM4InspectionDetailsForTests().IsEmpty());
	Toolkit->SetM4InspectionDetailsForTests(TEXT("old report"));
	Toolkit->Stop();
	TestTrue(TEXT("closing invalidates the report"), Toolkit->GetM4InspectionDetailsForTests().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CrossEditorControlChangeTest, "VoxelLand.M4.Editor.CrossEditorControlChange", M4EditorBakeFlags)
bool FVoxelLandM4CrossEditorControlChangeTest::RunTest(const FString& Parameters)
{
	UVoxelLandTerrainGraph* Control = NewObject<UVoxelLandTerrainGraph>();
	UVoxelLandTerrainGraph* Host = NewObject<UVoxelLandTerrainGraph>();
	UVoxelLandTerrainGraph* Unrelated = NewObject<UVoxelLandTerrainGraph>();
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Control;
	TSharedRef<FVoxelLandGraphToolkit> ControlEditor = MakeShared<FVoxelLandGraphToolkit>();
	TSharedRef<FVoxelLandGraphToolkit> PreviewEditor = MakeShared<FVoxelLandGraphToolkit>();
	ControlEditor->InitializeForTests(Control);
	PreviewEditor->InitializeForTests(Host);
	PreviewEditor->StartM4PreviewForTests(Config);
	if (!TestNotNull(TEXT("control graph editor exists"), Control->EditorGraph.Get()))
	{
		return false;
	}
	UVoxelLandEditorNode* Node = nullptr;
	for (UEdGraphNode* Candidate : Control->EditorGraph->Nodes)
	{
		if ((Node = Cast<UVoxelLandEditorNode>(Candidate)))
		{
			break;
		}
	}
	if (!TestNotNull(TEXT("control graph node exists"), Node))
	{
		return false;
	}
	uint64 Revision = PreviewEditor->GetPreviewRevisionForTests();
	Node->NodePosX += 10;
	Control->EditorGraph->NotifyGraphChanged();
	TestEqual(TEXT("layout-only edit does not invalidate M4"), PreviewEditor->GetPreviewRevisionForTests(), Revision);
	++Node->SeedSalt;
	Control->EditorGraph->NotifyGraphChanged();
	TestTrue(TEXT("semantic source edit invalidates another toolkit"),
		PreviewEditor->GetPreviewRevisionForTests() > Revision);
	Revision = PreviewEditor->GetPreviewRevisionForTests();
	FVoxelLandSerializedProgram Payload = Unrelated->CompiledProgram;
	++Payload.SourceFingerprint;
	FVoxelLandGraphToolkit::StoreCompiledProgram(*Unrelated, MoveTemp(Payload));
	TestEqual(TEXT("unrelated compile leaves M4 alone"), PreviewEditor->GetPreviewRevisionForTests(), Revision);
	Payload = Control->CompiledProgram;
	++Payload.SourceFingerprint;
	FVoxelLandGraphToolkit::StoreCompiledProgram(*Control, MoveTemp(Payload));
	TestTrue(TEXT("compiled control replacement retries M4"), PreviewEditor->GetPreviewRevisionForTests() > Revision);
	Revision = PreviewEditor->GetPreviewRevisionForTests();
	Payload = Control->CompiledProgram;
	FVoxelLandGraphToolkit::StoreCompiledProgram(*Control, MoveTemp(Payload));
	TestEqual(TEXT("unchanged payload does not notify"), PreviewEditor->GetPreviewRevisionForTests(), Revision);
	PreviewEditor->Stop();
	ControlEditor->Stop();
	return true;
}

namespace
{
class FM4LiveCurvePreviewCommand final : public IAutomationLatentCommand
{
public:
	FM4LiveCurvePreviewCommand(FAutomationTestBase* InTest, UVoxelLandWorldConfig* InConfig,
		TSharedRef<FVoxelLandGraphToolkit> InToolkit, int64 InSavedHash)
		: Test(InTest)
		, Config(InConfig)
		, Toolkit(InToolkit)
		, SavedHash(InSavedHash)
		, Started(FPlatformTime::Seconds())
	{
	}
	virtual bool Update() override
	{
		if (!IsValid(Config) || FPlatformTime::Seconds() - Started > 30.0)
		{
			Test->AddError(TEXT("live M4 curve preview timed out"));
			Cleanup();
			return true;
		}
		Toolkit->PollJobsForTests();
		if (Toolkit->HasM4PreviewWorkForTests())
		{
			return false;
		}
		if (Phase == 0)
		{
			if (!Test->TestEqual(TEXT("unsaved curve preview publishes"), Toolkit->GetPreviewPublishCountForTests(), 1))
			{
				Test->AddError(Toolkit->GetStatusForTests());
				Cleanup();
				return true;
			}
			FString Error;
			Test->TestTrue(TEXT("saved LUT remains valid after save"), Config->ValidateBakedCurves(Error));
			Test->TestNotEqual(
				TEXT("save published the new LUT"), Config->Continental.CookedCurve.SamplesHash, SavedHash);
			Toolkit->StartM4PreviewForTests(Config);
			++Config->Continental.CurveKeys.Last().OutputQ8;
			Phase = 1;
			return false;
		}
		Test->TestEqual(TEXT("superseded curve preview never publishes"), Toolkit->GetPreviewPublishCountForTests(), 1);
		Test->TestTrue(TEXT("superseded curve preview reports stale"),
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
	UVoxelLandWorldConfig* Config;
	TSharedRef<FVoxelLandGraphToolkit> Toolkit;
	int64 SavedHash;
	double Started;
	int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4LiveCurvePreviewTest, "VoxelLand.M4.Editor.LiveCurvePreview", M4EditorBakeFlags)
bool FVoxelLandM4LiveCurvePreviewTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
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
	const int64 SavedHash = Config->Continental.CookedCurve.SamplesHash;
	Config->Continental.CurveKeys.Last().OutputQ8 = 256;
	TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Graph);
	Toolkit->InitializePreviewForTests();
	Toolkit->StartM4PreviewForTests(Config);
	if (!TestTrue(TEXT("unsaved valid curve starts worker"), Toolkit->HasM4PreviewWorkForTests()))
	{
		AddError(Toolkit->GetStatusForTests());
		Toolkit->Stop();
		Config->RemoveFromRoot();
		return false;
	}
	TestFalse(TEXT("asset LUT remains stale before save"), Config->ValidateBakedCurves(Error));
	TestEqual(TEXT("preview does not synchronously overwrite asset LUT"), Config->Continental.CookedCurve.SamplesHash,
		SavedHash);
	FVoxelLandM4WorldSnapshot BeforeSave;
	if (!TestTrue(TEXT("capture in-flight authored settings"), Config->CaptureSnapshot(BeforeSave, Error))
		|| !TestTrue(TEXT("save-time curve bake succeeds"), VoxelLandM4Editor::BakeWorldConfig(*Config, Error)))
	{
		AddError(Error);
		Toolkit->Stop();
		Config->RemoveFromRoot();
		return false;
	}
	TestFalse(TEXT("strict runtime identity sees changed LUT"), Config->MatchesSnapshot(BeforeSave));
	TestTrue(TEXT("preview identity ignores only LUT replacement"), Config->MatchesPreviewSnapshot(BeforeSave));
	ADD_LATENT_AUTOMATION_COMMAND(FM4LiveCurvePreviewCommand(this, Config, Toolkit, SavedHash));
	return true;
}

#endif
