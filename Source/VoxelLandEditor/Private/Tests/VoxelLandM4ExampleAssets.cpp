#if WITH_DEV_AUTOMATION_TESTS
#include "../VoxelLandM4EditorBake.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "EdGraph/EdGraph.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
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
		if (!TestNotNull(TEXT("saved graph retains editor layout"), Graphs[I]->EditorGraph))
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
#endif
