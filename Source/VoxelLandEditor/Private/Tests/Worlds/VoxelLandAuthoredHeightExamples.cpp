#if WITH_DEV_AUTOMATION_TESTS

#include "../../Graph/Schema/VoxelLandEditorNode.h"
#include "../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"

class FVoxelLandAuthoredExampleBuilder final
{
public:
	explicit FVoxelLandAuthoredExampleBuilder(UVoxelLandTerrainGraph& InGraph)
		: Graph(InGraph)
	{
	}

	FGuid Build(bool bMountains)
	{
		using N = EVoxelLandBuiltinNode;
		const int32 GroundNoise = Noise(9600.f);
		if (!bMountains)
		{
			return Output(TEXT("Height"), GroundNoise);
		}

		const int32 Foothills = UnaryScale(GroundNoise, 0.35f);
		Output(TEXT("Foothills"), Foothills);

		const int32 MaskNoise = Noise(24000.f);
		const int32 ShiftedMask = Add(N::Add);
		Link(MaskNoise, ShiftedMask, 0);
		Link(Parameter(TEXT("MountainCoverage"), 0.6f, -1.f, 1.f), ShiftedMask, 1);
		const int32 Mask = Add(N::Max);
		Link(ShiftedMask, Mask, 0);
		const int32 MaskLimited = Add(N::Min);
		Link(Mask, MaskLimited, 0);
		Graph.Nodes[MaskLimited].Pins[1].FloatDefault = 1.f;
		Output(TEXT("Mountain Mask"), MaskLimited);

		const int32 RidgeNoise = Noise(6000.f);
		RidgeNode = Graph.Nodes[RidgeNoise].Id;
		const int32 Abs = Add(N::Abs);
		Link(RidgeNoise, Abs, 0);
		const int32 Ridged = Add(N::Subtract);
		Graph.Nodes[Ridged].Pins[0].FloatDefault = 1.f;
		Link(Abs, Ridged, 1);
		const int32 Ridges = Add(N::Multiply);
		Link(Ridged, Ridges, 0);
		Link(Parameter(TEXT("MountainHeight"), 3.f, 0.f, 8.f), Ridges, 1);
		Output(TEXT("Ridges"), Ridges);

		const int32 Relief = Add(N::Multiply);
		Link(Ridges, Relief, 0);
		Link(MaskLimited, Relief, 1);
		const int32 Height = Add(N::Add);
		Link(Foothills, Height, 0);
		Link(Relief, Height, 1);
		return Output(TEXT("Height"), Height);
	}

	void PrepareEditorGraph() const
	{
		const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
		Toolkit->InitializeForTests(&Graph);
		for (UEdGraphNode* Node : Graph.EditorGraph->Nodes)
		{
			if (Node->NodeGuid == RidgeNode)
			{
				Node->NodeComment = TEXT("Ridge Size · adjust Hill Size in the Inspector");
				Node->bCommentBubbleVisible = true;
			}
		}
		Toolkit->Stop();
	}

	static bool Save(UObject& Asset, bool bNewAsset = true)
	{
		UPackage* Package = Asset.GetOutermost();
		const FString Filename =
			FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(&Asset);
		}
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, &Asset, *Filename, Args);
	}

	static bool CheckChunks(const FVoxelLandGenerator& Generator, FString& Error)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Generator.CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		FVoxelLandChunkKey Origin;
		FVoxelLandChunkKey East;
		East.Coordinate.X = 1;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Left;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Right;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Regenerated;
		for (const FVoxelLandChunkKey& Key : {Origin, East, Origin})
		{
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
			if (const FVoxelLandError Failure = Generator.GenerateChunk(Key, *Worker, Chunk))
			{
				Error = Failure.Message;
				return false;
			}
			if (Key == East)
			{
				Right = MoveTemp(Chunk);
			}
			else if (!Left)
			{
				Left = MoveTemp(Chunk);
			}
			else
			{
				Regenerated = MoveTemp(Chunk);
			}
		}
		if (Left->Hash != Regenerated->Hash)
		{
			Error = TEXT("Authoring example changed when regenerating a chunk after its neighbor.");
			return false;
		}
		constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
		constexpr int32 Cells = FVoxelLandChunkData::Cells;
		constexpr int32 Overlap = Axis - Cells;
		for (int32 Z = 0; Z < Axis; ++Z)
		{
			for (int32 Y = 0; Y < Axis; ++Y)
			{
				const int32 Start = Axis * (Y + Axis * Z);
				if (FMemory::Memcmp(Left->Density.GetData() + Start + Cells, Right->Density.GetData() + Start,
						Overlap * sizeof(int16))
					!= 0)
				{
					Error = TEXT("Authoring example disagrees across the shared face and halo.");
					return false;
				}
			}
		}
		return true;
	}

	static FString AssetPath(const TCHAR* Prefix, const TCHAR* Example)
	{
		return FString::Printf(TEXT("/VoxelLand/Examples/%s_%s"), Prefix, Example);
	}

private:
	int32 Add(EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		verify(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
		const int32 Index = Graph.Nodes.Num();
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.Id = FGuid(0x564c4847, 1, 1, Index + 1);
		Node.OperatorId = Spec.OperatorId;
		Node.SeedSalt = uint32(Index + 1);
		for (const FVoxelLandBuiltinPinSpec& Input : Spec.Pins)
		{
			FVoxelLandGraphPinRecord& Pin = Node.Pins.AddDefaulted_GetRef();
			Pin.Id = FGuid(0x564c4850, 1, Index + 1, Node.Pins.Num());
			Pin.Name = Input.Name;
			Pin.Type = Input.Type;
			Pin.bOutput = Input.bOutput;
			Pin.FloatDefault = FCString::Atof(*Input.DefaultLiteral);
			Pin.IntDefault = FCString::Atoi(*Input.DefaultLiteral);
		}
		return Index;
	}

	int32 Noise(float SizeCm)
	{
		const int32 Node = Add(EVoxelLandBuiltinNode::WorldNoise2D);
		Graph.Nodes[Node].Pins[2].FloatDefault = 1.f / SizeCm;
		return Node;
	}

	int32 Parameter(const TCHAR* Name, float Default, float Minimum, float Maximum)
	{
		const int32 Node = Add(EVoxelLandBuiltinNode::FloatParameter);
		FVoxelLandGraphParameter& Value = Graph.Parameters.AddDefaulted_GetRef();
		Value.Id = FGuid(0x564c4856, 1, 1, Graph.Parameters.Num());
		Value.Name = Name;
		Value.FloatDefault = Default;
		Value.bHasRange = true;
		Value.FloatMin = Minimum;
		Value.FloatMax = Maximum;
		Graph.Nodes[Node].ParameterId = Value.Id;
		return Node;
	}

	int32 UnaryScale(int32 Source, float Scale)
	{
		const int32 Node = Add(EVoxelLandBuiltinNode::Multiply);
		Link(Source, Node, 0);
		Graph.Nodes[Node].Pins[1].FloatDefault = Scale;
		return Node;
	}

	FGuid Output(const TCHAR* Name, int32 Source)
	{
		const int32 Node = Add(EVoxelLandBuiltinNode::FloatOutput);
		FVoxelLandGraphOutputs::SetName(Graph.Nodes[Node], Name);
		Link(Source, Node, 0);
		return Graph.Nodes[Node].Id;
	}

	void Link(int32 Source, int32 Target, int32 Input)
	{
		FVoxelLandGraphLinkRecord& Wire = Graph.Links.AddDefaulted_GetRef();
		Wire.SourceNode = Graph.Nodes[Source].Id;
		Wire.SourcePin = Graph.Nodes[Source].Pins.Last().Id;
		Wire.TargetNode = Graph.Nodes[Target].Id;
		Wire.TargetPin = Graph.Nodes[Target].Pins[Input].Id;
	}

	UVoxelLandTerrainGraph& Graph;
	FGuid RidgeNode;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateAuthoredHeightExamples, "VoxelLand.AuthoredHeight.CreateExamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCreateAuthoredHeightExamples::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateAuthoredHeightExamples")))
	{
		return true;
	}
	for (const TCHAR* Example : {TEXT("RollingHills"), TEXT("MountainBiome")})
	{
		const FString GraphPath = FVoxelLandAuthoredExampleBuilder::AssetPath(TEXT("TG"), Example);
		const FString ConfigPath = FVoxelLandAuthoredExampleBuilder::AssetPath(TEXT("WC"), Example);
		if (FPackageName::DoesPackageExist(GraphPath) || FPackageName::DoesPackageExist(ConfigPath))
		{
			AddError(TEXT("Refusing to overwrite an existing example."));
			return false;
		}
		const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
		UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(
			CreatePackage(*GraphPath), FName(*FPackageName::GetShortName(GraphPath)), Flags);
		UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>(
			CreatePackage(*ConfigPath), FName(*FPackageName::GetShortName(ConfigPath)), Flags);
		Config->Graph = Graph;
		FVoxelLandAuthoredExampleBuilder Builder(*Graph);
		Config->TerrainOutputId = Builder.Build(FCString::Strcmp(Example, TEXT("MountainBiome")) == 0);
		Builder.PrepareEditorGraph();
		Config->TerrainOutputLabel = TEXT("Height");
		Config->HeightScaleCm = FCString::Strcmp(Example, TEXT("MountainBiome")) == 0 ? 1600 : 1800;
		Config->bGlobalErosion = false;
		Config->CellsX = Config->CellsY = 32;
		Config->OriginXCm = Config->OriginYCm = -51200;
		Config->GridSpacingCm = 3200;
		if (!FVoxelLandAuthoredExampleBuilder::Save(*Graph) || !FVoxelLandAuthoredExampleBuilder::Save(*Config))
		{
			AddError(FString::Printf(TEXT("Could not save %s."), Example));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandUpdateProjectHeightExamples,
	"VoxelLand.AuthoredHeight.UpdateProjectExamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandUpdateProjectHeightExamples::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandUpdateProjectHeightExamples")))
	{
		return true;
	}
	const TCHAR* ProjectConfigs[] = {TEXT("/Game/VoxelLandExamples/M4/DA_M4_HeightWorld.DA_M4_HeightWorld"),
		TEXT("/Game/VoxelLandExamples/M4/DA_InfiniteHeightWorld.DA_InfiniteHeightWorld")};
	const TCHAR* Templates[] = {TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"),
		TEXT("/VoxelLand/Examples/WC_MountainBiome.WC_MountainBiome")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ProjectConfigs); ++Index)
	{
		UVoxelLandWorldConfig* Config = LoadObject<UVoxelLandWorldConfig>(nullptr, ProjectConfigs[Index]);
		const UVoxelLandWorldConfig* Template = LoadObject<UVoxelLandWorldConfig>(nullptr, Templates[Index]);
		if (!TestNotNull(TEXT("Project example config exists"), Config)
			|| !TestNotNull(TEXT("Authored example template exists"), Template))
		{
			return false;
		}
		Config->Modify();
		Config->Graph = Template->Graph;
		Config->TerrainSource = EVoxelLandTerrainSource::HeightTerrain;
		Config->WorldExtent = Index == 0 ? EVoxelLandWorldExtent::Finite : EVoxelLandWorldExtent::Infinite;
		Config->TerrainOutputId = Template->TerrainOutputId;
		Config->TerrainOutputLabel = Template->TerrainOutputLabel;
		Config->HeightScaleCm = Template->HeightScaleCm;
		Config->Continental = {};
		Config->Mountain = {};
		Config->Peaks = {};
		FString Error;
		FVoxelLandM4WorldSnapshot Snapshot;
		if (!Config->CaptureSnapshot(Snapshot, Error))
		{
			AddError(Error);
			return false;
		}
		if (!TestTrue(TEXT("Project example now saves with an authored Height binding"),
				FVoxelLandAuthoredExampleBuilder::Save(*Config, false)))
		{
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandVerifyAuthoredHeightExamples, "VoxelLand.AuthoredHeight.Examples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandVerifyAuthoredHeightExamples::RunTest(const FString& Parameters)
{
	for (const TCHAR* Example : {TEXT("RollingHills"), TEXT("MountainBiome")})
	{
		const FString ConfigPath = FVoxelLandAuthoredExampleBuilder::AssetPath(TEXT("WC"), Example);
		const FString ObjectPath = ConfigPath + TEXT(".") + FPackageName::GetShortName(ConfigPath);
		UVoxelLandWorldConfig* Config = LoadObject<UVoxelLandWorldConfig>(nullptr, *ObjectPath);
		if (!TestNotNull(TEXT("Authored example loads"), Config)
			|| !TestNotNull(TEXT("Example graph loads"), Config->Graph.Get()))
		{
			return false;
		}
		if (FCString::Strcmp(Example, TEXT("RollingHills")) == 0)
		{
			TestEqual(TEXT("Rolling Hills has exactly two editable nodes"), Config->Graph->Nodes.Num(), 2);
		}
		for (const EVoxelLandWorldExtent Extent : {EVoxelLandWorldExtent::Finite, EVoxelLandWorldExtent::Infinite})
		{
			UVoxelLandWorldConfig* Preview = DuplicateObject<UVoxelLandWorldConfig>(Config, GetTransientPackage());
			Preview->WorldExtent = Extent;
			if (Extent == EVoxelLandWorldExtent::Finite)
			{
				Preview->bGlobalErosion = true;
				Preview->DropletCount = 128;
			}
			FVoxelLandM4WorldSnapshot Snapshot;
			FString Error;
			if (!Preview->CaptureSnapshot(Snapshot, Error))
			{
				AddError(Error);
				return false;
			}
			const auto Lease = VoxelLandM4Preparation::Reserve();
			if (!Async(EAsyncExecution::ThreadPool, [&Snapshot, &Lease, &Error] {
					TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
					if (!VoxelLandM4Preparation::Prepare(Snapshot, Lease, [] { return false; }, Generator, Error))
					{
						return false;
					}
					return FVoxelLandAuthoredExampleBuilder::CheckChunks(*Generator, Error);
				}).Get())
			{
				AddError(Error);
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandAuthoredOutputEditingTest, "VoxelLand.AuthoredHeight.OutputEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandAuthoredOutputEditingTest::RunTest(const FString& Parameters)
{
	UVoxelLandTerrainGraph* Asset =
		NewObject<UVoxelLandTerrainGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
	UVoxelLandWorldConfig* Config = NewObject<UVoxelLandWorldConfig>();
	Config->Graph = Asset;
	Config->TerrainOutputLabel = TEXT("Height");
	Config->TerrainOutputId = FVoxelLandAuthoredExampleBuilder(*Asset).Build(false);
	const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
	Toolkit->InitializeForTests(Asset);
	UEdGraph* Graph = Asset->EditorGraph;
	UVoxelLandEditorNode* Output = Cast<UVoxelLandEditorNode>(Graph->Nodes.Last());
	if (!TestNotNull(TEXT("Editable output exists"), Output))
	{
		Toolkit->Stop();
		return false;
	}
	const FGuid OutputId = Config->TerrainOutputId;
	FVoxelLandGraphSnapshot Source;
	FString Error;
	TestTrue(TEXT("Original Height binding resolves"), Config->CaptureBoundOutput(Source, Error));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "RenameHeightOutput", "Rename height output"));
		Output->Modify();
		Output->OutputName = TEXT("Elevation");
		Graph->NotifyGraphChanged();
	}
	TestEqual(TEXT("Rename preserves output ID"), Output->NodeGuid, OutputId);
	TestTrue(TEXT("Renamed output still resolves by ID"), Config->CaptureBoundOutput(Source, Error));
	{
		const FScopedTransaction Transaction(NSLOCTEXT("VoxelLandTests", "RemoveHeightOutput", "Remove height output"));
		Graph->Modify();
		Output->Modify();
		Output->BreakAllNodeLinks();
		Output->DestroyNode();
		Graph->NotifyGraphChanged();
	}
	TestFalse(TEXT("A deleted binding fails validation"), Config->CaptureBoundOutput(Source, Error));
	TestTrue(TEXT("Missing binding names its graph and output"),
		Error.Contains(Asset->GetName()) && Error.Contains(TEXT("Height")));
	TestTrue(TEXT("Undo restores deleted output"), GEditor->UndoTransaction());
	TestTrue(TEXT("Restored binding resolves"), Config->CaptureBoundOutput(Source, Error));
	TestTrue(TEXT("Undo restores the default output name"), GEditor->UndoTransaction());
	Toolkit->InitializePreviewForTests();
	Toolkit->RequestCompileForTests();
	const double TerrainDeadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < TerrainDeadline && Toolkit->GetPreviewPublishCountForTests() == 0)
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}
	TestTrue(*Toolkit->GetStatusForTests(), Toolkit->GetPreviewBoundsForTests().IsValid != 0);

	UVoxelLandEditorNode* Constant = NewObject<UVoxelLandEditorNode>(Graph);
	Constant->Kind = int32(EVoxelLandBuiltinNode::IntConstant);
	Constant->CreateNewGuid();
	Constant->AllocateDefaultPins();
	Constant->Pins[0]->DefaultValue = TEXT("2147483647");
	Graph->AddNode(Constant, false, false);
	UVoxelLandEditorNode* Category = NewObject<UVoxelLandEditorNode>(Graph);
	Category->Kind = int32(EVoxelLandBuiltinNode::IntOutput);
	Category->OutputName = TEXT("Category");
	Category->CreateNewGuid();
	Category->AllocateDefaultPins();
	Graph->AddNode(Category, false, false);
	Constant->Pins.Last()->MakeLinkTo(Category->Pins[0]);
	Graph->NotifyGraphChanged();
	Toolkit->SelectNamedOutputForTests(Category->NodeGuid, Category->OutputName);
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline
		   && (Toolkit->HasCompileWorkForTests() || Toolkit->HasM4PreviewWorkForTests()))
	{
		Toolkit->PollJobsForTests();
		FPlatformProcess::Sleep(0.005f);
	}
	const FString Status = Toolkit->GetStatusForTests();
	TestTrue(*Status, Status.Contains(TEXT("2147483647")));
	Toolkit->Stop();
	return true;
}

#endif
