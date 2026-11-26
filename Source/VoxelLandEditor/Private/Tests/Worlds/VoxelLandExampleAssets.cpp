#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "EdGraph/EdGraph.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "../../Graph/Schema/VoxelLandEditorNode.h"
#include "../../Graph/Schema/VoxelLandEditorSchema.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR GraphPath[] = TEXT("/VoxelLand/Examples/DA_M3_Hills");
constexpr TCHAR SettingsPath[] = TEXT("/VoxelLand/Examples/DA_DirectDensityWorld");
constexpr TCHAR MaterialPath[] = TEXT("/VoxelLand/Examples/DA_M3_MaterialSetup");
const FGuid AmplitudeId(0x4d334558, 1, 0, 1);
const FGuid OctavesId(0x4d334558, 1, 0, 2);
const FGuid SmoothingId(0x4d334558, 1, 0, 3);

class FExampleBuilder
{
public:
	explicit FExampleBuilder(UVoxelLandTerrainGraph& InAsset)
		: Asset(InAsset)
	{
		Graph = NewObject<UEdGraph>(&Asset, TEXT("ExampleGraph"), RF_Transactional);
		Graph->Schema = UVoxelLandEditorSchema::StaticClass();
		Asset.EditorGraph = Graph;
	}
	void Build()
	{
		AddParameters();
		AddNodes();
		ConnectNodes();
		CaptureSource();
	}

private:
	void AddParameters()
	{
		FVoxelLandGraphParameter Amplitude;
		Amplitude.Id = AmplitudeId;
		Amplitude.Name = TEXT("Amplitude");
		Amplitude.Type = EVoxelLandGraphValueType::Float;
		Amplitude.FloatDefault = 700.0f;
		Amplitude.bHasRange = true;
		Amplitude.FloatMin = 0.0f;
		Amplitude.FloatMax = 3000.0f;
		Amplitude.Tooltip = NSLOCTEXT("VoxelLandExample", "Amplitude", "Primary hills amplitude in centimetres.");
		Asset.Parameters.Add(Amplitude);
		FVoxelLandGraphParameter Octaves;
		Octaves.Id = OctavesId;
		Octaves.Name = TEXT("Octaves");
		Octaves.Type = EVoxelLandGraphValueType::Int32;
		Octaves.IntDefault = 4;
		Octaves.bHasRange = true;
		Octaves.IntMin = 1;
		Octaves.IntMax = 8;
		Octaves.Tooltip = NSLOCTEXT("VoxelLandExample", "Octaves", "Octaves for the independently seeded fBm branch.");
		Asset.Parameters.Add(Octaves);
		FVoxelLandGraphParameter Smoothing;
		Smoothing.Id = SmoothingId;
		Smoothing.Name = TEXT("SmoothingEnabled");
		Smoothing.Type = EVoxelLandGraphValueType::Bool;
		Smoothing.BoolDefault = true;
		Smoothing.Tooltip = NSLOCTEXT("VoxelLandExample", "Smoothing", "Enable the one-cell density filter.");
		Asset.Parameters.Add(Smoothing);
	}
	UVoxelLandEditorNode* Add(EVoxelLandBuiltinNode Kind, int32 X, int32 Y, FGuid ParameterId = FGuid())
	{
		UVoxelLandEditorNode* Node = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
		Node->Kind = int32(Kind);
		Node->NodeGuid = FGuid(0x4d334e44, 1, 0, Nodes.Num() + 1);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Node->ParameterId = ParameterId;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		for (int32 Index = 0; Index < Node->Pins.Num(); ++Index)
		{
			Node->Pins[Index]->PinId = FGuid(0x4d33504e, 1, Nodes.Num() + 1, Index + 1);
		}
		Nodes.Add(Node);
		return Node;
	}
	void AddNodes()
	{
		Add(EVoxelLandBuiltinNode::PositionX, -1200, -450);
		Add(EVoxelLandBuiltinNode::PositionY, -1200, -260);
		Add(EVoxelLandBuiltinNode::Perlin2, -950, -500);
		Add(EVoxelLandBuiltinNode::FloatParameter, -950, -210, AmplitudeId);
		Add(EVoxelLandBuiltinNode::Multiply, -650, -420);
		Add(EVoxelLandBuiltinNode::Fbm2, -950, 40);
		Add(EVoxelLandBuiltinNode::IntParameter, -1200, 280, OctavesId);
		Add(EVoxelLandBuiltinNode::Multiply, -650, 100);
		Add(EVoxelLandBuiltinNode::Add, -380, -200);
		Add(EVoxelLandBuiltinNode::HeightToDensity, -100, -200);
		Add(EVoxelLandBuiltinNode::BoolParameter, -100, 100, SmoothingId);
		Add(EVoxelLandBuiltinNode::SmoothDensity, 200, -200);
		Add(EVoxelLandBuiltinNode::DensityOutput, 500, -200);
		SetDefault(2, TEXT("Frequency"), TEXT("0.001"));
		SetDefault(5, TEXT("Frequency"), TEXT("0.00035"));
		SetDefault(7, TEXT("Input 2"), TEXT("350"));
		SetDefault(11, TEXT("Strength"), TEXT("0.35"));
		Nodes[2]->SeedSalt = 11;
		Nodes[5]->SeedSalt = 29;
	}
	void SetDefault(int32 NodeIndex, const TCHAR* PinName, const TCHAR* Value)
	{
		UEdGraphPin* Pin = Nodes[NodeIndex]->FindPin(FName(PinName), EGPD_Input);
		check(Pin);
		Pin->DefaultValue = Value;
	}
	void Wire(int32 Source, int32 Target, const TCHAR* InputName)
	{
		UEdGraphPin* Output = Nodes[Source]->FindPin(TEXT("Result"), EGPD_Output);
		UEdGraphPin* Input = Nodes[Target]->FindPin(FName(InputName), EGPD_Input);
		check(Output && Input);
		Output->MakeLinkTo(Input);
	}
	void ConnectNodes()
	{
		Wire(0, 2, TEXT("X"));
		Wire(1, 2, TEXT("Y"));
		Wire(2, 4, TEXT("Input 1"));
		Wire(3, 4, TEXT("Input 2"));
		Wire(0, 5, TEXT("X"));
		Wire(1, 5, TEXT("Y"));
		Wire(6, 5, TEXT("Octaves"));
		Wire(5, 7, TEXT("Input 1"));
		Wire(4, 8, TEXT("Input 1"));
		Wire(7, 8, TEXT("Input 2"));
		Wire(8, 9, TEXT("Value"));
		Wire(9, 11, TEXT("Density"));
		Wire(10, 11, TEXT("Enabled"));
		Wire(11, 12, TEXT("Density"));
	}
	void CaptureSource()
	{
		for (UVoxelLandEditorNode* Node : Nodes)
		{
			FVoxelLandGraphNodeRecord Record;
			Record.Id = Node->NodeGuid;
			Record.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode(Node->Kind));
			Record.OperatorVersion = Node->OperatorVersion;
			Record.ParameterId = Node->ParameterId;
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
	UVoxelLandTerrainGraph& Asset;
	UEdGraph* Graph = nullptr;
	TArray<UVoxelLandEditorNode*> Nodes;
};
bool ExamplePathsAreFree(FString& Error)
{
	const TCHAR* Paths[] = {GraphPath, SettingsPath, MaterialPath};
	for (const TCHAR* Path : Paths)
	{
		if (FindPackage(nullptr, Path) || FPackageName::DoesPackageExist(Path))
		{
			Error = FString::Printf(TEXT("Refusing to overwrite an existing example package: %s"), Path);
			return false;
		}
	}
	return true;
}
bool CompileExample(UVoxelLandTerrainGraph& Graph, FString& Error)
{
	FVoxelLandGraphSnapshot Snapshot;
	if (!Graph.CaptureSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandSerializedProgram Payload;
	TFuture<bool> Work = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
		FVoxelLandProgramDraft Draft;
		if (!FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program);
		if (Failure)
		{
			Error = Failure.Message;
			return false;
		}
		return FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
	});
	if (!Work.Get())
	{
		return false;
	}
	Graph.CompiledProgram = MoveTemp(Payload);
	return true;
}
bool SaveExample(UObject* Asset, FString& Error)
{
	UPackage* Package = Asset->GetOutermost();
	const FString Filename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
	{
		Error = FString::Printf(TEXT("Could not create example directory for %s"), *Filename);
		return false;
	}
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
	{
		Error = FString::Printf(TEXT("Could not save %s"), *Filename);
		return false;
	}
	return true;
}
bool CheckProgram(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandSerializedProgram& Payload, uint64& OutHash,
	FString& Error)
{
	TFuture<bool> Work = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &OutHash, &Error]() {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, Error))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.WorldSeed = 1234;
		Settings.Profile.VoxelSizeCm = 100.0f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
		{
			Error = Failure.Message;
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Generator->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(0, 0, -1);
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		if (const FVoxelLandError Failure = Generator->GenerateChunk(Key, *Worker, Chunk))
		{
			Error = Failure.Message;
			return false;
		}
		OutHash = Chunk->Hash;
		return true;
	});
	return Work.Get();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateExampleAssets, "VoxelLand.Examples.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandCreateExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateExamples")))
	{
		AddInfo(TEXT("Example creation skipped; pass -VoxelLandCreateExamples to enable it explicitly."));
		return true;
	}
	FString Error;
	if (!ExamplePathsAreFree(Error))
	{
		AddError(Error);
		return false;
	}
	UPackage* GraphPackage = CreatePackage(GraphPath);
	UPackage* SettingsPackage = CreatePackage(SettingsPath);
	UPackage* MaterialPackage = CreatePackage(MaterialPath);
	const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(GraphPackage, TEXT("DA_M3_Hills"), Flags);
	UVoxelLandWorldConfig* Settings =
		NewObject<UVoxelLandWorldConfig>(SettingsPackage, TEXT("DA_DirectDensityWorld"), Flags);
	UVoxelLandMaterialSetup* Material =
		NewObject<UVoxelLandMaterialSetup>(MaterialPackage, TEXT("DA_M3_MaterialSetup"), Flags);
	FExampleBuilder(*Graph).Build();
	Settings->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
	Settings->Graph = Graph;
	Settings->Seed = 1234;
	Settings->DirectVoxelSizeCm = 100.0f;
	Settings->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
	Settings->WorldExtent = EVoxelLandWorldExtent::Infinite;
	Settings->MaterialSetup = Material;
	Material->Material = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));
	if (!Settings->ValidateDirectDensitySettings(Error) || !CompileExample(*Graph, Error))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandGraphSnapshot Snapshot;
	uint64 Hash = 0;
	if (!Graph->CaptureSnapshot(Snapshot, Error) || !CheckProgram(Snapshot, Graph->CompiledProgram, Hash, Error))
	{
		AddError(Error);
		return false;
	}
	if (!SaveExample(Graph, Error) || !SaveExample(Material, Error) || !SaveExample(Settings, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("Created example assets; reference chunk hash %llu"), static_cast<unsigned long long>(Hash)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandVerifyExampleAssets, "VoxelLand.Examples.Verify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandVerifyExampleAssets::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandVerifyExamples")))
	{
		AddInfo(TEXT("Example verification skipped; pass -VoxelLandVerifyExamples to enable it explicitly."));
		return true;
	}
	UVoxelLandTerrainGraph* Graph =
		LoadObject<UVoxelLandTerrainGraph>(nullptr, TEXT("/VoxelLand/Examples/DA_M3_Hills.DA_M3_Hills"));
	UVoxelLandWorldConfig* Settings = LoadObject<UVoxelLandWorldConfig>(
		nullptr, TEXT("/VoxelLand/Examples/DA_DirectDensityWorld.DA_DirectDensityWorld"));
	UVoxelLandMaterialSetup* Material = LoadObject<UVoxelLandMaterialSetup>(
		nullptr, TEXT("/VoxelLand/Examples/DA_M3_MaterialSetup.DA_M3_MaterialSetup"));
	if (!Graph || !Settings || !Material)
	{
		AddError(TEXT("Could not reopen all three example assets."));
		return false;
	}
	FString Error;
	if (Settings->Graph != Graph || Settings->MaterialSetup.LoadSynchronous() != Material
		|| !Settings->ValidateDirectDensitySettings(Error))
	{
		AddError(FString::Printf(TEXT("Example settings references or values are invalid: %s"), *Error));
		return false;
	}
	UMaterialInterface* SurfaceMaterial = Material->Material.LoadSynchronous();
	if (!SurfaceMaterial || !SurfaceMaterial->GetMaterial()
		|| SurfaceMaterial->GetMaterial()->MaterialDomain != MD_Surface)
	{
		AddError(TEXT("The example material setup does not resolve to a surface material."));
		return false;
	}
	if (Graph->Parameters.Num() != 3 || Graph->CompiledProgram.Noise.Num() != 2
		|| !Graph->CompiledProgram.SmoothFilter.bPresent || !Graph->EditorGraph)
	{
		AddError(TEXT("Reopened example is missing typed parameters, mixed noise, the filter, or editor layout."));
		return false;
	}
	const FVoxelLandGraphParameter* Amplitude =
		Graph->Parameters.FindByPredicate([](const FVoxelLandGraphParameter& P) { return P.Id == AmplitudeId; });
	const FVoxelLandGraphParameter* Octaves =
		Graph->Parameters.FindByPredicate([](const FVoxelLandGraphParameter& P) { return P.Id == OctavesId; });
	const FVoxelLandGraphParameter* Smoothing =
		Graph->Parameters.FindByPredicate([](const FVoxelLandGraphParameter& P) { return P.Id == SmoothingId; });
	const int32 SeedA = Graph->CompiledProgram.Noise[0].SeedSlot;
	const int32 SeedB = Graph->CompiledProgram.Noise[1].SeedSlot;
	if (!Amplitude || Amplitude->Type != EVoxelLandGraphValueType::Float || !Octaves
		|| Octaves->Type != EVoxelLandGraphValueType::Int32 || !Smoothing
		|| Smoothing->Type != EVoxelLandGraphValueType::Bool || !Graph->CompiledProgram.Seeds.IsValidIndex(SeedA)
		|| !Graph->CompiledProgram.Seeds.IsValidIndex(SeedB) || SeedA == SeedB
		|| Graph->CompiledProgram.Seeds[SeedA].NodeId == Graph->CompiledProgram.Seeds[SeedB].NodeId)
	{
		AddError(TEXT("Reopened example lacks the intended typed parameters or independent noise seed slots."));
		return false;
	}
	FVoxelLandGraphSnapshot Snapshot;
	uint64 Hash = 0;
	if (!Graph->CaptureSnapshot(Snapshot, Error) || !CheckProgram(Snapshot, Graph->CompiledProgram, Hash, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("Reopened example assets; reference chunk hash %llu"), static_cast<unsigned long long>(Hash)));
	return true;
}
#endif
