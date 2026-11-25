#if WITH_DEV_AUTOMATION_TESTS
#include "../../../VoxelLandEditorBake.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Terrain/Controls/VoxelLandControlGraph.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightStageValidation.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "EdGraph/EdGraph.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "../../../Graph/Schema/VoxelLandEditorNode.h"
#include "../../../Graph/Schema/VoxelLandEditorSchema.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR M4ExampleRoot[] = TEXT("/Game/VoxelLandExamples/M4/");
const TCHAR* M4ExampleNames[] = {
	TEXT("DA_M4_Continental"), TEXT("DA_M4_Mountain"), TEXT("DA_M4_Peaks"), TEXT("DA_M4_World")};
constexpr EAutomationTestFlags M4AssetFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

void M4ExampleCaptureNodes(UVoxelLandTerrainGraph& Asset, TConstArrayView<UVoxelLandEditorNode*> Nodes)
{
	for (UVoxelLandEditorNode* Node : Nodes)
	{
		FVoxelLandGraphNodeRecord Record;
		Record.Id = Node->NodeGuid;
		Record.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode(Node->Kind));
		Record.OperatorVersion = Node->OperatorVersion;
		Record.SeedSalt = Node->SeedSalt;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			FVoxelLandGraphPinRecord Data;
			Data.Id = Pin->PinId;
			Data.Name = Pin->PinName.ToString();
			Data.bOutput = Pin->Direction == EGPD_Output;
			Data.Type = Pin->PinType.PinCategory == TEXT("int")  ? EVoxelLandGraphValueType::Int32
					  : Pin->PinType.PinCategory == TEXT("bool") ? EVoxelLandGraphValueType::Bool
																 : EVoxelLandGraphValueType::Float;
			if (!Data.bOutput)
			{
				if (Data.Type == EVoxelLandGraphValueType::Float)
				{
					Data.FloatDefault = FCString::Atof(*Pin->DefaultValue);
				}
				else if (Data.Type == EVoxelLandGraphValueType::Int32)
				{
					Data.IntDefault = FCString::Atoi(*Pin->DefaultValue);
				}
				else
				{
					Data.BoolDefault = Pin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
				}
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					FVoxelLandGraphLinkRecord Link;
					Link.SourceNode = Linked->GetOwningNode()->NodeGuid;
					Link.SourcePin = Linked->PinId;
					Link.TargetNode = Node->NodeGuid;
					Link.TargetPin = Pin->PinId;
					Asset.Links.Add(Link);
				}
			}
			Record.Pins.Add(MoveTemp(Data));
		}
		Asset.Nodes.Add(MoveTemp(Record));
	}
}

bool M4ExampleBuildGraph(UVoxelLandTerrainGraph& Asset, int32 Channel, FString& Error)
{
	UEdGraph* Graph = NewObject<UEdGraph>(&Asset, TEXT("ExampleGraph"), RF_Transactional);
	Graph->Schema = UVoxelLandEditorSchema::StaticClass();
	Asset.EditorGraph = Graph;
	const EVoxelLandBuiltinNode Kinds[] = {EVoxelLandBuiltinNode::PositionX, EVoxelLandBuiltinNode::PositionY,
		EVoxelLandBuiltinNode::Fbm2, EVoxelLandBuiltinNode::DensityOutput};
	const TCHAR* Frequencies[] = {TEXT("0.00002"), TEXT("0.00006"), TEXT("0.00012")};
	UVoxelLandEditorNode* Nodes[4] = {};
	for (int32 I = 0; I < 4; ++I)
	{
		Nodes[I] = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
		Nodes[I]->Kind = int32(Kinds[I]);
		Nodes[I]->NodeGuid = FGuid(0x4d344558, Channel + 1, 1, I + 1);
		Nodes[I]->NodePosX = I < 2 ? -600 : (I == 2 ? -300 : 0);
		Nodes[I]->NodePosY = I == 1 ? 180 : 0;
		Nodes[I]->SeedSalt = 17 + Channel * 29;
		Graph->AddNode(Nodes[I], false, false);
		Nodes[I]->AllocateDefaultPins();
		for (int32 P = 0; P < Nodes[I]->Pins.Num(); ++P)
		{
			Nodes[I]->Pins[P]->PinId = FGuid(0x4d345049, Channel + 1, I + 1, P + 1);
		}
	}
	UEdGraphPin* Frequency = Nodes[2]->FindPin(TEXT("Frequency"), EGPD_Input);
	if (!Frequency)
	{
		Error = TEXT("M4 example frequency pin is unavailable");
		return false;
	}
	Frequency->DefaultValue = Frequencies[Channel];
	const int32 From[] = {0, 1, 2};
	const int32 To[] = {2, 2, 3};
	const TCHAR* InputNames[] = {TEXT("X"), TEXT("Y"), TEXT("Density")};
	for (int32 I = 0; I < 3; ++I)
	{
		UEdGraphPin* Output = Nodes[From[I]]->FindPin(TEXT("Result"), EGPD_Output);
		UEdGraphPin* Input = Nodes[To[I]]->FindPin(FName(InputNames[I]), EGPD_Input);
		if (!Output || !Input)
		{
			Error = TEXT("M4 example graph pin is unavailable");
			return false;
		}
		Output->MakeLinkTo(Input);
	}
	M4ExampleCaptureNodes(Asset, Nodes);
	FVoxelLandGraphSnapshot Snapshot;
	if (!Asset.CaptureSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandSerializedProgram Payload;
	bool bCompiled = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		return FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error) && !FVoxelLandProgram::Validate(Draft, Program)
			&& FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
	}).Get();
	if (bCompiled)
	{
		Asset.CompiledProgram = MoveTemp(Payload);
	}
	return bCompiled;
}

bool M4ExampleBuildHeightGraph(UVoxelLandM4ControlGraph& Asset, FString& Error)
{
	UEdGraph* Graph = NewObject<UEdGraph>(&Asset, TEXT("ExampleGraph"), RF_Transactional);
	Graph->Schema = UVoxelLandEditorSchema::StaticClass();
	Asset.EditorGraph = Graph;
	const EVoxelLandBuiltinNode Kinds[] = {EVoxelLandBuiltinNode::PositionX, EVoxelLandBuiltinNode::PositionY,
		EVoxelLandBuiltinNode::Fbm2, EVoxelLandBuiltinNode::Fbm2, EVoxelLandBuiltinNode::Fbm2,
		EVoxelLandBuiltinNode::M4HeightOutput};
	const TCHAR* Frequencies[] = {TEXT("0.00002"), TEXT("0.00006"), TEXT("0.00012")};
	const TCHAR* Inputs[] = {TEXT("Continental"), TEXT("Mountain"), TEXT("Peaks")};
	TArray<UVoxelLandEditorNode*> Nodes;
	for (int32 I = 0; I < 6; ++I)
	{
		UVoxelLandEditorNode* Node = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
		Node->Kind = int32(Kinds[I]);
		Node->NodeGuid = FGuid(0x4d344854, 1, 0, I + 1);
		Node->NodePosX = I < 2 ? -700 : I < 5 ? -350 : 0;
		Node->NodePosY = I < 2 ? I * 180 : I < 5 ? (I - 3) * 240 : 0;
		Node->SeedSalt = I >= 2 && I < 5 ? 17 + (I - 2) * 29 : 0;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		for (int32 P = 0; P < Node->Pins.Num(); ++P)
		{
			Node->Pins[P]->PinId = FGuid(0x4d344850, 1, I + 1, P + 1);
		}
		Nodes.Add(Node);
	}
	Asset.HeightOutputId = Nodes[5]->NodeGuid;
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		UVoxelLandEditorNode* Noise = Nodes[Channel + 2];
		UEdGraphPin* Frequency = Noise->FindPin(TEXT("Frequency"), EGPD_Input);
		UEdGraphPin* X = Noise->FindPin(TEXT("X"), EGPD_Input);
		UEdGraphPin* Y = Noise->FindPin(TEXT("Y"), EGPD_Input);
		UEdGraphPin* Value = Noise->FindPin(TEXT("Result"), EGPD_Output);
		UEdGraphPin* XOutput = Nodes[0]->FindPin(TEXT("Result"), EGPD_Output);
		UEdGraphPin* YOutput = Nodes[1]->FindPin(TEXT("Result"), EGPD_Output);
		UEdGraphPin* Target = Nodes[5]->FindPin(FName(Inputs[Channel]), EGPD_Input);
		if (!Frequency || !X || !Y || !Value || !XOutput || !YOutput || !Target)
		{
			Error = TEXT("M4 shared height example graph pins are unavailable");
			return false;
		}
		Frequency->DefaultValue = Frequencies[Channel];
		XOutput->MakeLinkTo(X);
		YOutput->MakeLinkTo(Y);
		Value->MakeLinkTo(Target);
	}
	M4ExampleCaptureNodes(Asset, Nodes);
	return VoxelLandM4Editor::CompileNamedGraph(Asset, Error);
}

void M4ExampleSetCurve(FVoxelLandM4ControlConfig& Channel, int32 First, int32 Mid, int32 Last)
{
	FVoxelLandM4CurveAuthoringKey A, B, C;
	A.InputQ16 = 0;
	A.OutputQ8 = First;
	B.InputQ16 = 32768;
	B.OutputQ8 = Mid;
	C.InputQ16 = 65535;
	C.OutputQ8 = Last;
	Channel.CurveKeys = {A, B, C};
}

bool M4ExampleConfigure(UVoxelLandM4WorldConfig& Config, UVoxelLandTerrainGraph* const (&Graphs)[3], FString& Error)
{
	Config.Continental.Graph = Graphs[0];
	Config.Mountain.Graph = Graphs[1];
	Config.Peaks.Graph = Graphs[2];
	M4ExampleSetCurve(Config.Continental, -3072000, 0, 6144000);
	M4ExampleSetCurve(Config.Mountain, 0, 512000, 3584000);
	M4ExampleSetCurve(Config.Peaks, -768000, 0, 1536000);
	Config.Seed = 271828;
	Config.DropletCount = 16;
	Config.MaxSteps = 8;
	Config.ThermalIterations = 1;
	Config.HeightDetail.bEnabled = true;
	Config.HeightDetail.AmplitudeQ8 = 25600;
	Config.HeightDetail.FrequencyPerCm = 0.001f;
	Config.bOverhangEnabled = true;
	Config.OverhangAmplitudeQ8 = 16384;
	return Config.ValidateSettings(Error) && VoxelLandM4Editor::BakeWorldConfig(Config, Error);
}

bool M4ExampleCheckHeight(UVoxelLandM4WorldConfig& Config, UVoxelLandM4ControlGraph& Graph, FString& Error)
{
	if (Config.Continental.Graph != &Graph || Config.Mountain.Graph != &Graph || Config.Peaks.Graph != &Graph
		|| !Graph.HeightOutputId.IsValid() || !Graph.EditorGraph || !Config.bGlobalErosion
		|| !Config.HeightDetail.bEnabled || !Config.ValidateBakedCurves(Error))
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("M4 height example references or settings are invalid");
		}
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!Config.CaptureSnapshot(Snapshot, Error) || Snapshot.HeightStage.Version != 1
		|| !VoxelLandM4HeightStageValidation::Validate(Snapshot, Error))
	{
		return false;
	}
	return Async(EAsyncExecution::ThreadPool, [&Snapshot, &Error] {
		const FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		for (const FVoxelLandM4ControlSnapshot* Control : Controls)
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandProgramCodec::Load(Control->Source, Control->Payload, Program, Error))
			{
				return false;
			}
			if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Program))
			{
				Error = Failure.Message;
				return false;
			}
		}
		return true;
	}).Get();
}

bool M4ExampleSave(UObject* Asset, FString& Error)
{
	UPackage* Package = Asset->GetOutermost();
	FString Filename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
	{
		Error = FString::Printf(TEXT("Cannot create example directory: %s"), *Filename);
		return false;
	}
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
	{
		Error = FString::Printf(TEXT("Cannot save example: %s"), *Filename);
		return false;
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ExampleAuthoring, "VoxelLand.M4.Example.Authoring", M4AssetFlags)

bool FVoxelLandM4ExampleAuthoring::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	UVoxelLandTerrainGraph* Graphs[3] = {NewObject<UVoxelLandTerrainGraph>(Config),
		NewObject<UVoxelLandTerrainGraph>(Config), NewObject<UVoxelLandTerrainGraph>(Config)};
	FString Error;
	for (int32 I = 0; I < 3; ++I)
	{
		if (!TestTrue(TEXT("example control compiles"), M4ExampleBuildGraph(*Graphs[I], I, Error)))
		{
			AddError(Error);
			return false;
		}
	}
	if (!TestTrue(TEXT("example config and curves bake"), M4ExampleConfigure(*Config, Graphs, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("baked example remains current"), Config->ValidateBakedCurves(Error));
	TestTrue(TEXT("example uses three distinct graphs"),
		Graphs[0] != Graphs[1] && Graphs[1] != Graphs[2] && Graphs[0] != Graphs[2]);
	TestTrue(TEXT("example includes erosion and detail"),
		Config->bGlobalErosion && Config->HeightDetail.bEnabled && Config->HeightDetail.AmplitudeQ8 > 0);
	Config->Peaks.CurveKeys[1].OutputQ8++;
	TestFalse(TEXT("editing curve invalidates baked example"), Config->ValidateBakedCurves(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CreateExampleAssets, "VoxelLand.M4.Example.CreateAssets", M4AssetFlags)

bool FVoxelLandM4CreateExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateM4Examples")))
	{
		AddInfo(TEXT("Skipped; pass -VoxelLandCreateM4Examples to create assets at unused paths."));
		return true;
	}
	FString Paths[4];
	for (int32 I = 0; I < 4; ++I)
	{
		Paths[I] = FString(M4ExampleRoot) + M4ExampleNames[I];
		if (FindPackage(nullptr, *Paths[I]) || FPackageName::DoesPackageExist(Paths[I]))
		{
			AddError(FString::Printf(TEXT("Refusing to overwrite existing package %s"), *Paths[I]));
			return false;
		}
	}
	const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
	UVoxelLandTerrainGraph* Graphs[3];
	FString Error;
	for (int32 I = 0; I < 3; ++I)
	{
		Graphs[I] = NewObject<UVoxelLandTerrainGraph>(CreatePackage(*Paths[I]), FName(M4ExampleNames[I]), Flags);
		if (!M4ExampleBuildGraph(*Graphs[I], I, Error))
		{
			AddError(Error);
			return false;
		}
	}
	UVoxelLandM4WorldConfig* Config =
		NewObject<UVoxelLandM4WorldConfig>(CreatePackage(*Paths[3]), FName(M4ExampleNames[3]), Flags);
	if (!M4ExampleConfigure(*Config, Graphs, Error))
	{
		AddError(Error);
		return false;
	}
	for (UVoxelLandTerrainGraph* Graph : Graphs)
	{
		if (!M4ExampleSave(Graph, Error))
		{
			AddError(Error);
			return false;
		}
	}
	if (!M4ExampleSave(Config, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(TEXT("Created four M4 example packages under /Game/VoxelLandExamples/M4."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4VerifyExampleAssets, "VoxelLand.M4.Example.VerifyAssets", M4AssetFlags)

bool FVoxelLandM4VerifyExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandVerifyM4Examples")))
	{
		AddInfo(TEXT("Skipped; pass -VoxelLandVerifyM4Examples in a fresh editor process to verify saved assets."));
		return true;
	}
	UVoxelLandTerrainGraph* Graphs[3] = {};
	for (int32 I = 0; I < 3; ++I)
	{
		const FString ObjectPath = FString(M4ExampleRoot) + M4ExampleNames[I] + TEXT(".") + M4ExampleNames[I];
		Graphs[I] = LoadObject<UVoxelLandTerrainGraph>(nullptr, *ObjectPath);
		if (!TestNotNull(TEXT("saved control graph loads"), Graphs[I]))
		{
			return false;
		}
		if (!TestNotNull(TEXT("saved graph retains editor layout"), Graphs[I]->EditorGraph.Get()))
		{
			return false;
		}
	}
	const FString ConfigPath = FString(M4ExampleRoot) + M4ExampleNames[3] + TEXT(".") + M4ExampleNames[3];
	UVoxelLandM4WorldConfig* Config = LoadObject<UVoxelLandM4WorldConfig>(nullptr, *ConfigPath);
	if (!TestNotNull(TEXT("saved M4 config loads"), Config))
	{
		return false;
	}
	FString Error;
	if (!TestTrue(TEXT("saved references and features match"),
			Config->Continental.Graph == Graphs[0] && Config->Mountain.Graph == Graphs[1]
				&& Config->Peaks.Graph == Graphs[2] && Config->bGlobalErosion && Config->HeightDetail.bEnabled
				&& Config->HeightDetail.AmplitudeQ8 == 25600))
	{
		return false;
	}
	if (!TestTrue(TEXT("saved configuration validates"), Config->ValidateSettings(Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("saved baked curves validate"), Config->ValidateBakedCurves(Error)))
	{
		AddError(Error);
		return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightExampleAuthoring, "VoxelLand.M4.Example.HeightAuthoring", M4AssetFlags)

bool FVoxelLandM4HeightExampleAuthoring::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	UVoxelLandM4ControlGraph* Graph = NewObject<UVoxelLandM4ControlGraph>(Config);
	FString Error;
	if (!TestTrue(TEXT("editable shared height graph builds"), M4ExampleBuildHeightGraph(*Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	UVoxelLandTerrainGraph* Graphs[3] = {Graph, Graph, Graph};
	if (!TestTrue(TEXT("height example config bakes"), M4ExampleConfigure(*Config, Graphs, Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("height example projected programs validate"), M4ExampleCheckHeight(*Config, *Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("height graph has three branches and one terminal"), Graph->Nodes.Num(), 6);
	TestEqual(TEXT("height graph retains all nine editable links"), Graph->Links.Num(), 9);
	Config->Peaks.CurveKeys[1].OutputQ8++;
	TestFalse(TEXT("height example detects stale curve"), Config->ValidateBakedCurves(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CreateHeightExampleAssets, "VoxelLand.M4.Example.CreateHeightAssets", M4AssetFlags)

bool FVoxelLandM4CreateHeightExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateM4HeightExamples")))
	{
		AddInfo(TEXT("Skipped; pass -VoxelLandCreateM4HeightExamples to create assets at unused paths."));
		return true;
	}
	const TCHAR* Names[] = {TEXT("DA_M4_HeightGraph"), TEXT("DA_M4_HeightWorld")};
	FString Paths[2];
	for (int32 I = 0; I < 2; ++I)
	{
		Paths[I] = FString(M4ExampleRoot) + Names[I];
		if (FindPackage(nullptr, *Paths[I]) || FPackageName::DoesPackageExist(Paths[I]))
		{
			AddError(FString::Printf(TEXT("Refusing to overwrite existing package %s"), *Paths[I]));
			return false;
		}
	}
	const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
	UVoxelLandM4ControlGraph* Graph =
		NewObject<UVoxelLandM4ControlGraph>(CreatePackage(*Paths[0]), FName(Names[0]), Flags);
	UVoxelLandM4WorldConfig* Config =
		NewObject<UVoxelLandM4WorldConfig>(CreatePackage(*Paths[1]), FName(Names[1]), Flags);
	UVoxelLandTerrainGraph* Graphs[3] = {Graph, Graph, Graph};
	FString Error;
	if (!M4ExampleBuildHeightGraph(*Graph, Error) || !M4ExampleConfigure(*Config, Graphs, Error)
		|| !M4ExampleCheckHeight(*Config, *Graph, Error) || !M4ExampleSave(Graph, Error)
		|| !M4ExampleSave(Config, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(TEXT("Created shared M4 height graph and world config in /Game/VoxelLandExamples/M4."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4VerifyHeightExampleAssets, "VoxelLand.M4.Example.VerifyHeightAssets", M4AssetFlags)

bool FVoxelLandM4VerifyHeightExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandVerifyM4HeightExamples")))
	{
		AddInfo(TEXT("Skipped; pass -VoxelLandVerifyM4HeightExamples in a fresh editor process."));
		return true;
	}
	UVoxelLandM4ControlGraph* Graph = LoadObject<UVoxelLandM4ControlGraph>(
		nullptr, TEXT("/Game/VoxelLandExamples/M4/DA_M4_HeightGraph.DA_M4_HeightGraph"));
	UVoxelLandM4WorldConfig* Config = LoadObject<UVoxelLandM4WorldConfig>(
		nullptr, TEXT("/Game/VoxelLandExamples/M4/DA_M4_HeightWorld.DA_M4_HeightWorld"));
	if (!TestNotNull(TEXT("saved shared graph loads"), Graph)
		|| !TestNotNull(TEXT("saved height config loads"), Config))
	{
		return false;
	}
	FString Error;
	if (!TestTrue(
			TEXT("saved height graph and projections remain valid"), M4ExampleCheckHeight(*Config, *Graph, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("saved graph retains editable layout"), Graph->EditorGraph->Nodes.Num(), 6);
	return true;
}

#endif