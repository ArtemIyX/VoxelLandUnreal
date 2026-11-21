#include "Graph/VoxelLandGraphToolkit.h"
#include "Operators/VoxelLandOperator.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/PackageReload.h"
#include "RenderCommandFence.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandReloadTestBarrier.h"
#endif
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandPreviewViewport.h"
#include "Graph/VoxelLandPreviewScene.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandMeshing.h"
#include "Rendering/VoxelLandChunkComponent.h"
#include "Rendering/VoxelLandPreviewUploadObservation.h"
#include "HAL/IConsoleManager.h"
#include "AdvancedPreviewScene.h"
#include "AssetRegistry/AssetData.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "PropertyCustomizationHelpers.h"
#include "Async/Async.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "GraphEditAction.h"
#include "Editor.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "EdGraphNode_Comment.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "SGraphActionMenu.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Containers/Ticker.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "VoxelLandGraphToolkit"
namespace
{
TAutoConsoleVariable<int32> CVarVoxelLandPreviewGPUUploadTiming(
    TEXT("VoxelLand.Preview.GPUUploadTiming"), 0,
    TEXT("Observe preview buffer upload GPU fences on supported D3D12 editor previews."), ECVF_Default);
#if WITH_DEV_AUTOMATION_TESTS
TAutoConsoleVariable<int32> CVarVoxelLandPreviewGPUUploadHoldForTests(
    TEXT("VoxelLand.Preview.GPUUploadHoldForTests"), 0,
    TEXT("Keep the preview upload observation pending for lifecycle automation."), ECVF_Default);
#endif
const FName GraphTab(TEXT("VoxelLandGraph"));
const FName DetailsTab(TEXT("VoxelLandDetails"));
const FName DiagnosticsTab(TEXT("VoxelLandDiagnostics"));
const FName PreviewTab(TEXT("VoxelLandPreview"));
const FName PaletteTab(TEXT("VoxelLandPalette"));

EVoxelLandGraphValueType PinType(const UEdGraphPin& Pin)
{
    if (Pin.PinType.PinCategory == TEXT("bool"))
    {
        return EVoxelLandGraphValueType::Bool;
    }
    if (Pin.PinType.PinCategory == TEXT("int"))
    {
        return EVoxelLandGraphValueType::Int32;
    }
    return EVoxelLandGraphValueType::Float;
}
FName Category(EVoxelLandGraphValueType Type)
{
    if (Type == EVoxelLandGraphValueType::Bool)
    {
        return TEXT("bool");
    }
    return Type == EVoxelLandGraphValueType::Int32 ? FName(TEXT("int")) : FName(TEXT("float"));
}
bool CopyPin(const UEdGraphPin& Pin, FVoxelLandGraphPinRecord& Record)
{
    Record.Id = Pin.PinId;
    Record.Name = Pin.PinName.ToString();
    Record.bOutput = Pin.Direction == EGPD_Output;
    Record.Type = PinType(Pin);
    if (Record.bOutput)
    {
        return true;
    }
    if (Record.Type == EVoxelLandGraphValueType::Float)
    {
        if (!LexTryParseString(Record.FloatDefault, *Pin.DefaultValue))
        {
            return false;
        }
        return FMath::IsFinite(Record.FloatDefault);
    }
    else if (Record.Type == EVoxelLandGraphValueType::Int32)
    {
        return LexTryParseString(Record.IntDefault, *Pin.DefaultValue);
    }
    else
    {
        if (Pin.DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Pin.DefaultValue == TEXT("1"))
        {
            Record.BoolDefault = true;
        }
        else if (!Pin.DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) && Pin.DefaultValue != TEXT("0"))
        {
            return false;
        }
    }
    return true;
}
void RestorePin(UVoxelLandEditorNode& Node, const FVoxelLandGraphPinRecord& Record)
{
    UEdGraphPin* Pin = Node.CreatePin(Record.bOutput ? EGPD_Output : EGPD_Input, Category(Record.Type), FName(*Record.Name));
    Pin->PinId = Record.Id;
    if (Record.Type == EVoxelLandGraphValueType::Float)
    {
        Pin->DefaultValue = FVoxelLandEditorFloat::Format(Record.FloatDefault);
    }
    else if (Record.Type == EVoxelLandGraphValueType::Int32)
    {
        Pin->DefaultValue = LexToString(Record.IntDefault);
    }
    else
    {
        Pin->DefaultValue = Record.BoolDefault ? TEXT("true") : TEXT("false");
    }
}
bool CaptureNode(const UVoxelLandEditorNode& Node, TArray<FVoxelLandGraphNodeRecord>& Nodes,
    TArray<FVoxelLandGraphLinkRecord>& Links, bool& bValidDefaults, FString& Error, FVoxelLandGraphDiagnostic* Diagnostic)
{
    FVoxelLandGraphNodeRecord Record;
    Record.Id = Node.NodeGuid;
    Record.OperatorId = Node.Kind == 0 ? Node.ExternalOperatorId : VoxelLandBuiltinNodeId(static_cast<EVoxelLandBuiltinNode>(Node.Kind));
    Record.OperatorVersion = Node.OperatorVersion;
    Record.ParameterId = Node.ParameterId;
    Record.SeedSalt = Node.SeedSalt;
    Record.Configuration = Node.Configuration;
    for (const UEdGraphPin* Pin : Node.Pins)
    {
        if (!Pin)
        {
            continue;
        }
        FVoxelLandGraphPinRecord PinRecord;
        if (!CopyPin(*Pin, PinRecord))
        {
            if (bValidDefaults)
            {
                Error = FString::Printf(TEXT("Invalid default for pin %s."), *Pin->PinName.ToString());
                if (Diagnostic)
                {
                    Diagnostic->Message = Error; Diagnostic->NodeId = Node.NodeGuid; Diagnostic->PinId = Pin->PinId;
                }
            }
            PinRecord.bInvalidDefault = true;
            bValidDefaults = false;
        }
        Record.Pins.Add(PinRecord);
        if (Pin->Direction == EGPD_Input)
        {
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                const UVoxelLandEditorNode* Source = Linked ? Cast<UVoxelLandEditorNode>(Linked->GetOwningNode()) : nullptr;
                if (!Source)
                {
                    Error = TEXT("Only terrain graph node pins may be connected.");
                    if (Diagnostic) { Diagnostic->Message = Error; Diagnostic->NodeId = Node.NodeGuid; Diagnostic->PinId = Pin->PinId; }
                    return false;
                }
                FVoxelLandGraphLinkRecord Link;
                Link.SourceNode = Source->NodeGuid;
                Link.SourcePin = Linked->PinId;
                Link.TargetNode = Node.NodeGuid;
                Link.TargetPin = Pin->PinId;
                Links.Add(Link);
            }
        }
    }
    Nodes.Add(MoveTemp(Record));
    return true;
}

class FVoxelLandPreviewJob
{
public:
    using FResult = TArray<TPair<FIntVector, TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>>>;

    static bool Run(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandSerializedProgram& Payload,
        int64 Seed, const FVoxelLandZoneProfile& Profile, int32 HorizontalRadius, int32 VerticalRadius,
        const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel, FResult& Meshes, FString& Error
#if WITH_DEV_AUTOMATION_TESTS
        , const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>& Barrier
#endif
        )
    {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        if (!FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, Error))
        {
            return false;
        }
        FVoxelLandGeneratorSettings Settings;
        Settings.Program = Program;
        Settings.WorldSeed = uint64(Seed);
        Settings.Profile = Profile;
        TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
        if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
        {
            Error = Failure.Message;
            return false;
        }
        TUniquePtr<FVoxelLandWorkerContext> Worker;
        if (FVoxelLandError Failure = Generator->CreateWorker(Worker))
        {
            Error = Failure.Message;
            return false;
        }
#if WITH_DEV_AUTOMATION_TESTS
        if (Barrier)
        {
            Barrier->WaitForCancellation(Cancel);
            if (Cancel->Load()) { return false; }
        }
#endif
        FVoxelLandMeshingScratch Scratch;
        for (int32 Z = -VerticalRadius; Z < VerticalRadius; ++Z)
        {
            for (int32 Y = -HorizontalRadius; Y <= HorizontalRadius; ++Y)
            {
                for (int32 X = -HorizontalRadius; X <= HorizontalRadius; ++X)
                {
                    if (!Generate(*Generator, *Worker, Scratch, FIntVector(X, Y, Z), Profile.VoxelSizeCm, Cancel, Meshes, Error))
                    {
                        return false;
                    }
                }
            }
        }
        return !Cancel->Load();
    }
private:
    static bool Generate(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker,
        FVoxelLandMeshingScratch& Scratch, FIntVector Position, float VoxelSize, const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
        FResult& Meshes, FString& Error)
    {
        if (Cancel->Load())
        {
            return false;
        }
        FVoxelLandChunkKey Key;
        Key.Coordinate = Position;
        TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
        if (FVoxelLandError Failure = Generator.GenerateChunkCancellable(Key, Worker, Chunk, [&Cancel] { return Cancel->Load(); }))
        {
            Error = Failure.Message;
            return false;
        }
        TSharedPtr<FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
        const EVoxelLandMeshResult Result = VoxelLandMeshing::BuildSurfaceNets(*Chunk, VoxelSize, Scratch, *Mesh, [&Cancel] { return Cancel->Load(); });
        if (Result != EVoxelLandMeshResult::Success)
        {
            Error = TEXT("Preview meshing failed or was cancelled.");
            return false;
        }
        if (!Mesh->Indices.IsEmpty())
        {
            Meshes.Emplace(Position, Mesh);
        }
        return true;
    }
};
}

class FVoxelLandCompileWork
{
public:
    TAtomic<bool> Done{false};
    uint64 Revision = 0;
    bool bSuccess = false;
    FString Error;
    FVoxelLandGraphDiagnostic Diagnostic;
    FVoxelLandSerializedProgram Payload;
    double CompileMs = 0;
};
class FVoxelLandPreviewWork
{
public:
    TAtomic<bool> Done{false};
    uint64 PreviewRevision = 0;
    uint64 SourceRevision = 0;
    float VoxelSize = 100.0f;
    int32 ChunkCount = 18;
    bool bSuccess = false;
    FString Error;
    double GenerationMs = 0;
#if WITH_DEV_AUTOMATION_TESTS
    int64 SeedForTests = 0;
#endif
    FVoxelLandPreviewJob::FResult Meshes;
};

class FVoxelLandPreviewFence
{
public:
    FRenderCommandFence Fence;
    uint64 PreviewRevision = 0;
    uint64 SourceRevision = 0;
    int32 NonemptyChunks = 0;
    int32 ChunkCount = 0;
    double SemanticStart = 0;
    double SetupCompleted = 0;
    double CompileMs = 0;
    double GenerationMs = 0;
    double SetupMs = 0;
    bool bGPURequested = false;
    bool bRenderComplete = false;
    FString ReadyStatus;
    TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> UploadObservation;
};

FVoxelLandGraphToolkit::~FVoxelLandGraphToolkit()
{
    Stop();
}
void FVoxelLandGraphToolkit::Stop()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    if (bRegisteredForUndo && GEditor)
    {
        GEditor->UnregisterForUndo(this);
        bRegisteredForUndo = false;
    }
    FCoreUObjectDelegates::OnPackageReloaded.Remove(PackageReloadHandle);
    FCoreUObjectDelegates::OnObjectsReplaced.Remove(ObjectsReplacedHandle);
    FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ObjectsReinstancedHandle);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(MaterialSetupChangedHandle);
    MaterialSetupChangedHandle.Reset();
    FEditorDelegates::PreBeginPIE.Remove(BeginPIEHandle);
    FEditorDelegates::EndPIE.Remove(EndPIEHandle);
    BeginPIEHandle.Reset();
    EndPIEHandle.Reset();
    if (Graph && GraphChangedHandle.IsValid())
    {
        Graph->RemoveOnGraphChangedHandler(GraphChangedHandle);
        GraphChangedHandle.Reset();
    }
    if (Details)
    {
        Details->OnFinishedChangingProperties().RemoveAll(this);
    }
    if (PreviewDetails)
    {
        PreviewDetails->OnFinishedChangingProperties().RemoveAll(this);
    }
    CancelPreview();
    FTSTicker::RemoveTicker(PollHandle);
    for (const TFuture<void>& Task : WorkerTasks)
    {
        Task.Wait();
    }
    WorkerTasks.Reset();
    CompileWork.Reset();
    PreviewWork.Reset();
    ClearPreview();
}
void FVoxelLandGraphToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Asset);
    Collector.AddReferencedObject(Graph);
    Collector.AddReferencedObjects(PreviewComponents);
    Collector.AddReferencedObject(PreviewMaterial);
    Collector.AddReferencedObject(PreviewSettings);
}
FText FVoxelLandGraphToolkit::GetBaseToolkitName() const
{
    return LOCTEXT("Title", "VoxelLand Terrain Graph");
}
bool FVoxelLandGraphToolkit::StoreCompiledProgram(UVoxelLandTerrainGraph& Graph, FVoxelLandSerializedProgram&& Payload)
{
    if (FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(&Graph.CompiledProgram, &Payload, 0))
    {
        return false;
    }
    Graph.Modify();
    Graph.CompiledProgram = MoveTemp(Payload);
    Graph.MarkPackageDirty();
    return true;
}
void FVoxelLandGraphToolkit::Open(UVoxelLandTerrainGraph* InAsset, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host)
{
    Asset = InAsset;
    MaterialSetupChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FVoxelLandGraphToolkit::MaterialSetupPropertyChanged);
    PackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &FVoxelLandGraphToolkit::PackageReloaded);
    ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FVoxelLandGraphToolkit::ObjectsReplaced);
    ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this, &FVoxelLandGraphToolkit::ObjectsReinstanced);
    if (GEditor)
    {
        GEditor->RegisterForUndo(this);
        bRegisteredForUndo = true;
    }
    PreviewScene = MakeShared<FVoxelLandPreviewScene>();
    PreviewViewport = SNew(SVoxelLandPreviewViewport).Scene(PreviewScene);
    PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
    PreviewSettings->Graph = Asset;
    SynchronizePreviewSettings(false);
    ApplyPreviewMaterial(nullptr);
    CreateMirror();
    FVoxelLandGraphSnapshot InitialSource;
    InitialSource.SourceVersion = Asset->SourceVersion;
    InitialSource.Parameters = Asset->Parameters;
    InitialSource.Nodes = Asset->Nodes;
    InitialSource.Links = Asset->Links;
    LastSourceHash = FVoxelLandProgramCodec::SourceFingerprint(InitialSource);
    FString InitialError;
    CaptureMirror(InitialError);
    BindCommands();
    FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    Details = PropertyModule.CreateDetailView(FDetailsViewArgs());
    PreviewDetails = PropertyModule.CreateDetailView(FDetailsViewArgs());
    PreviewDetails->SetObject(PreviewSettings);
    PreviewDetails->OnFinishedChangingProperties().AddRaw(this, &FVoxelLandGraphToolkit::PreviewPropertiesChanged);
    Details->SetObject(Asset);
    Details->OnFinishedChangingProperties().AddRaw(this, &FVoxelLandGraphToolkit::PropertiesChanged);
    SGraphEditor::FGraphEditorEvents Events;
    Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FVoxelLandGraphToolkit::SelectionChanged);
    GraphWidget = SNew(SGraphEditor).AdditionalCommands(Commands).GraphToEdit(Graph).GraphEvents(Events).IsEditable(true);
    GraphChangedHandle = Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FVoxelLandGraphToolkit::GraphChanged));
    PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FVoxelLandGraphToolkit::PollJobs), 0.05f);
    BeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FVoxelLandGraphToolkit::BeginPIE);
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FVoxelLandGraphToolkit::EndPIE);
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("VoxelLandGraphEditor_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
            ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.50f)->AddTab(GraphTab, ETabState::OpenedTab))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.30f)->AddTab(PreviewTab, ETabState::OpenedTab))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.20f)->AddTab(DetailsTab, ETabState::OpenedTab)->AddTab(PaletteTab, ETabState::OpenedTab)))
            ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.2f)->AddTab(DiagnosticsTab, ETabState::OpenedTab)));
    InitAssetEditor(Mode, Host, TEXT("VoxelLandGraphEditor"), Layout, true, true, Asset);
    bHasEditorWindow = true;
    ExtendGraphToolbar();
    RegenerateMenusAndToolbars();
    RequestCompile();
}
#if WITH_DEV_AUTOMATION_TESTS
void FVoxelLandGraphToolkit::SetMaterialPickerForTests(UMaterialInterface* Material)
{
    MaterialChanged(Material ? FAssetData(Material) : FAssetData());
}
void FVoxelLandGraphToolkit::SetSettingsPickerForTests(UVoxelLandWorldSettings* Settings)
{
    SettingsChanged(Settings ? FAssetData(Settings) : FAssetData());
}
void FVoxelLandGraphToolkit::InitializePreviewForTests()
{
    PreviewScene = MakeShared<FVoxelLandPreviewScene>();
    PreviewViewport = SNew(SVoxelLandPreviewViewport).Scene(PreviewScene);
    ApplyPreviewMaterial(nullptr);
    PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FVoxelLandGraphToolkit::PollJobs), 0.05f);
    BeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FVoxelLandGraphToolkit::BeginPIE);
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FVoxelLandGraphToolkit::EndPIE);
}
FBox FVoxelLandGraphToolkit::GetPreviewBoundsForTests() const
{
    return PreviewViewport ? PreviewViewport->GetTerrainBounds() : FBox(ForceInit);
}
FBox FVoxelLandGraphToolkit::GetComponentBoundsForTests() const
{
    FBox Bounds(ForceInit);
    for (const UVoxelLandChunkComponent* Component : PreviewComponents)
    {
        if (Component)
        {
            Bounds += Component->Bounds.GetBox();
        }
    }
    return Bounds;
}
bool FVoxelLandGraphToolkit::FramePreviewForTests()
{
    return !bStopped && PreviewViewport && PreviewViewport->FrameTerrain();
}
void FVoxelLandGraphToolkit::CreatePanelForTests()
{
    GraphWidget = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(true);
}
void FVoxelLandGraphToolkit::CopyPasteNodesForTests(const TArray<UEdGraphNode*>& Nodes)
{
    GraphWidget->ClearSelectionSet();
    for (UEdGraphNode* Node : Nodes)
    {
        GraphWidget->SetNodeSelection(Node, true);
    }
    CopySelected();
    PasteNodes();
}
void FVoxelLandGraphToolkit::InitializeForTests(UVoxelLandTerrainGraph* InAsset)
{
    Asset = InAsset;
    MaterialSetupChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FVoxelLandGraphToolkit::MaterialSetupPropertyChanged);
    PackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &FVoxelLandGraphToolkit::PackageReloaded);
    ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FVoxelLandGraphToolkit::ObjectsReplaced);
    ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this, &FVoxelLandGraphToolkit::ObjectsReinstanced);
    PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
    PreviewSettings->Graph = Asset;
    SynchronizePreviewSettings(false);
    GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->NotifyAssetOpened(Asset, this);
    CreateMirror();
    FString Error;
    CaptureMirror(Error);
    GraphChangedHandle = Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FVoxelLandGraphToolkit::GraphChanged));
    GEditor->RegisterForUndo(this);
    bRegisteredForUndo = true;
}
#endif
void FVoxelLandGraphToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    Manager->RegisterTabSpawner(GraphTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnGraph)).SetDisplayName(LOCTEXT("Graph", "Graph"));
    Manager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnDetails)).SetDisplayName(LOCTEXT("Details", "Details / Parameters"));
    Manager->RegisterTabSpawner(DiagnosticsTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnDiagnostics)).SetDisplayName(LOCTEXT("Diagnostics", "Compiler Results"));
    Manager->RegisterTabSpawner(PreviewTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnPreview)).SetDisplayName(LOCTEXT("Preview", "Preview"));
    Manager->RegisterTabSpawner(PaletteTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnPalette)).SetDisplayName(LOCTEXT("Palette", "Node Palette"));
}
void FVoxelLandGraphToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    Manager->UnregisterTabSpawner(GraphTab); Manager->UnregisterTabSpawner(DetailsTab); Manager->UnregisterTabSpawner(DiagnosticsTab);
    Manager->UnregisterTabSpawner(PreviewTab);
    Manager->UnregisterTabSpawner(PaletteTab);
    FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}
TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnGraph(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("FrameSelection", "Frame selection")).OnClicked_Lambda([this]
        {
            FrameSelection();
            return FReply::Handled();
        })]
        + SVerticalBox::Slot().FillHeight(1)[GraphWidget.ToSharedRef()]];
}
TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnPalette(const FSpawnTabArgs&)
{
    PresentationRevision = FVoxelLandEditorOperatorRegistry::GetRevision();
    SAssignNew(PaletteWidget, SGraphActionMenu).GraphObj(Graph).ShowFilterTextBox(true)
        .OnCollectAllActions_Raw(this, &FVoxelLandGraphToolkit::CollectPalette)
        .OnActionDoubleClicked_Raw(this, &FVoxelLandGraphToolkit::ActivatePalette);
    return SNew(SDockTab)[PaletteWidget.ToSharedRef()];
}
void FVoxelLandGraphToolkit::CollectPalette(FGraphActionListBuilderBase& OutActions)
{
    FGraphContextMenuBuilder Context(Graph);
    Graph->GetSchema()->GetGraphContextActions(Context);
    OutActions.Append(Context);
}
void FVoxelLandGraphToolkit::ActivatePalette(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions)
{
    if (!GraphWidget || Actions.IsEmpty() || !Actions[0])
    {
        return;
    }
    Actions[0]->PerformAction(Graph, static_cast<UEdGraphPin*>(nullptr), GraphWidget->GetPasteLocation2f());
}
TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnDetails(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("ParameterHint", "Select the background for graph parameters, or a parameter node to choose its ID.")) .AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("AddFloat", "+ Float")).OnClicked_Lambda([this] { return AddParameter(EVoxelLandGraphValueType::Float); })]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("AddInt", "+ Int")).OnClicked_Lambda([this] { return AddParameter(EVoxelLandGraphValueType::Int32); })]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("AddBool", "+ Bool")).OnClicked_Lambda([this] { return AddParameter(EVoxelLandGraphValueType::Bool); })]]
        + SVerticalBox::Slot().FillHeight(1)[Details.ToSharedRef()]];
}
FReply FVoxelLandGraphToolkit::AddParameter(EVoxelLandGraphValueType Type)
{
    const FScopedTransaction Transaction(LOCTEXT("AddParameter", "Add terrain graph parameter"));
    Asset->Modify();
    FVoxelLandGraphParameter Parameter;
    Parameter.Id = FGuid::NewGuid();
    Parameter.Type = Type;
    int32 Suffix = Asset->Parameters.Num() + 1;
    do
    {
        Parameter.Name = FString::Printf(TEXT("Parameter%d"), Suffix++);
    }
    while (Asset->Parameters.ContainsByPredicate([&Parameter](const FVoxelLandGraphParameter& Existing)
    {
        return Existing.Name.Equals(Parameter.Name, ESearchCase::IgnoreCase);
    }));
    Asset->Parameters.Add(MoveTemp(Parameter));
    Details->SetObject(Asset, true);
    GraphChanged(FEdGraphEditAction());
    return FReply::Handled();
}
TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnDiagnostics(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("Compile", "Compile")) .OnClicked_Raw(this, &FVoxelLandGraphToolkit::CompileClicked)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("FocusError", "Focus error node")).OnClicked_Raw(this, &FVoxelLandGraphToolkit::FocusError)]
        + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text_Raw(this, &FVoxelLandGraphToolkit::StatusText)]];
}
FText FVoxelLandGraphToolkit::StatusText() const
{
    return FText::FromString(MaterialDiagnostic.IsEmpty() ? Status : Status + TEXT(" ") + MaterialDiagnostic);
}
FReply FVoxelLandGraphToolkit::CompileClicked() { RequestCompile(); return FReply::Handled(); }
bool FVoxelLandGraphToolkit::ResolveDiagnosticTarget(UEdGraphNode*& OutNode, UEdGraphPin*& OutPin) const
{
    OutNode = nullptr;
    OutPin = nullptr;
    if (!Graph || !ErrorDiagnostic.NodeId.IsValid()) { return false; }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid == ErrorDiagnostic.NodeId)
        {
            OutNode = Node;
            if (ErrorDiagnostic.PinId.IsValid())
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->PinId == ErrorDiagnostic.PinId) { OutPin = Pin; break; }
                }
            }
            return true;
        }
    }
    return false;
}
FReply FVoxelLandGraphToolkit::FocusError()
{
    UEdGraphNode* Node = nullptr;
    UEdGraphPin* Pin = nullptr;
    if (GraphWidget && ResolveDiagnosticTarget(Node, Pin))
    {
        if (Pin) { GraphWidget->JumpToPin(Pin); }
        else { GraphWidget->JumpToNode(Node); }
    }
    return FReply::Handled();
}
TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnPreview(const FSpawnTabArgs&)
{
    return SNew(SDockTab)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Raw(this, &FVoxelLandGraphToolkit::StatusText).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Seed", "Seed"))]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).Text_Lambda([this] { return FText::AsCultureInvariant(LexToString(PreviewSeed)); }).OnTextCommitted_Raw(this, &FVoxelLandGraphToolkit::SeedCommitted)]
            + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("FrameTerrain", "Frame terrain (F)")).OnClicked_Raw(this, &FVoxelLandGraphToolkit::FramePreviewTerrain)]
            + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ResetCamera", "Reset camera")).OnClicked_Raw(this, &FVoxelLandGraphToolkit::ResetPreviewCamera)]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(UMaterialInterface::StaticClass()).ObjectPath_Raw(this, &FVoxelLandGraphToolkit::MaterialPickerPath).OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::MaterialChanged)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(UVoxelLandWorldSettings::StaticClass()).ObjectPath_Raw(this, &FVoxelLandGraphToolkit::SettingsPickerPath).OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::SettingsChanged)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Radius", "Preview radius (XY / Z)"))]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(2).Value_Lambda([this] { return PreviewHorizontalRadius; }).OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { CommitPreviewRadius(Value, true); })]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(2).Value_Lambda([this] { return PreviewVerticalRadius; }).OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { CommitPreviewRadius(Value, false); })]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(LOCTEXT("PreviewSettings", "Transient preview settings and overrides"))].BodyContent()[SNew(SBox).MaxDesiredHeight(250)[PreviewDetails.ToSharedRef()]]]
        + SVerticalBox::Slot().FillHeight(1)[PreviewViewport.ToSharedRef()]];
}

void FVoxelLandGraphToolkit::CreateMirror()
{
    bLoading = true;
    if (Asset->EditorGraph)
    {
        Graph = Asset->EditorGraph;
        bLoading = false;
        return;
    }
    Asset->Modify();
    Graph = NewObject<UEdGraph>(Asset, NAME_None, RF_Transactional);
    Graph->Schema = UVoxelLandEditorSchema::StaticClass();
    Asset->EditorGraph = Graph;
    TMap<FGuid, UEdGraphPin*> PinsById;
    for (const FVoxelLandGraphNodeRecord& Record : Asset->Nodes)
    {
        UVoxelLandEditorNode* Node = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
        Node->NodeGuid = Record.Id;
        if (Record.OperatorId.A == 0x564c4d33 && Record.OperatorId.B == 1 && Record.OperatorId.C == 0)
        {
            Node->Kind = int32(Record.OperatorId.D);
        }
        else
        {
            Node->Kind = 0;
            Node->ExternalOperatorId = Record.OperatorId;
            Node->ExternalPins = Record.Pins;
            TArray<FVoxelLandEditorOperatorPresentation> Presentations;
            FVoxelLandEditorOperatorRegistry::GetPresentations(Presentations);
            for (const FVoxelLandEditorOperatorPresentation& Presentation : Presentations)
            {
                if (Presentation.OperatorId == Record.OperatorId && Presentation.Version == Record.OperatorVersion)
                {
                    Node->ExternalTitle = Presentation.DisplayName;
                    break;
                }
            }
        }
        Node->OperatorVersion = Record.OperatorVersion;
        Node->ParameterId = Record.ParameterId;
        Node->SeedSalt = Record.SeedSalt;
        Node->Configuration = Record.Configuration;
        Node->VariadicInputs = FMath::Max(2, Record.Pins.Num() - 1);
        Graph->AddNode(Node, false, false);
        for (const FVoxelLandGraphPinRecord& Pin : Record.Pins)
        {
            RestorePin(*Node, Pin);
            PinsById.Add(Pin.Id, Node->Pins.Last());
        }
    }
    for (const FVoxelLandGraphLinkRecord& Link : Asset->Links)
    {
        UEdGraphPin* const* Source = PinsById.Find(Link.SourcePin);
        UEdGraphPin* const* Target = PinsById.Find(Link.TargetPin);
        if (Source && Target)
        {
            (*Source)->MakeLinkTo(*Target);
        }
    }
    if (Asset->Nodes.IsEmpty())
    {
        UVoxelLandEditorNode* Output = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
        Output->Kind = int32(EVoxelLandBuiltinNode::DensityOutput);
        Output->CreateNewGuid();
        Graph->AddNode(Output, false, false);
        Output->AllocateDefaultPins();
    }
    Asset->MarkPackageDirty();
    bLoading = false;
}

bool FVoxelLandGraphToolkit::CaptureMirror(FString& Error, FVoxelLandGraphDiagnostic* Diagnostic)
{
    if (Diagnostic) { *Diagnostic = FVoxelLandGraphDiagnostic(); }
    if (!Asset || !Graph)
    {
        Error = TEXT("Missing graph asset or editor mirror.");
        return false;
    }
    TArray<FVoxelLandGraphNodeRecord> Nodes;
    TArray<FVoxelLandGraphLinkRecord> Links;
    bool bValidDefaults = true;
    for (UEdGraphNode* GraphNode : Graph->Nodes)
    {
        const UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(GraphNode);
        if (!Node)
        {
            continue;
        }
        if (!CaptureNode(*Node, Nodes, Links, bValidDefaults, Error, Diagnostic))
        {
            return false;
        }
    }
    FVoxelLandGraphSnapshot Snapshot;
    Snapshot.SourceVersion = Asset->SourceVersion;
    Snapshot.Parameters = Asset->Parameters;
    Snapshot.Nodes = Nodes;
    Snapshot.Links = Links;
    const uint64 NewHash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
    FVoxelLandGraphSnapshot Stored;
    Stored.SourceVersion = Asset->SourceVersion;
    Stored.Parameters = Asset->Parameters;
    Stored.Nodes = Asset->Nodes;
    Stored.Links = Asset->Links;
    if (FVoxelLandProgramCodec::SourceFingerprint(Stored) != NewHash)
    {
        if (!GIsTransacting)
        {
            Asset->Modify();
        }
        Asset->Nodes = MoveTemp(Nodes);
        Asset->Links = MoveTemp(Links);
        Asset->MarkPackageDirty();
    }
    if (NewHash != LastSourceHash)
    {
        LastSourceHash = NewHash;
        ++Revision;
        Status = TEXT("Stale: graph changed.");
    }
    return bValidDefaults;
}
void FVoxelLandGraphToolkit::PostUndo(bool bSuccess)
{
    if (bSuccess && !bStopped)
    {
        SynchronizeGraph();
        SynchronizePreviewSettings(true);
        if (PreviewDetails)
        {
            PreviewDetails->ForceRefresh();
        }
        if (GraphWidget && Graph)
        {
            const TSet<UObject*> Selection = GraphWidget->GetSelectedNodes();
            for (UObject* Object : Selection)
            {
                UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
                if (Node && !Graph->Nodes.Contains(Node))
                {
                    GraphWidget->SetNodeSelection(Node, false);
                }
            }
            TGuardValue<bool> Guard(bLoading, true);
            GraphWidget->NotifyGraphChanged();
#if WITH_DEV_AUTOMATION_TESTS
            ++PanelRefreshCountForTests;
#endif
        }
    }
}
void FVoxelLandGraphToolkit::PostRedo(bool bSuccess)
{
    PostUndo(bSuccess);
}
void FVoxelLandGraphToolkit::GraphChanged(const FEdGraphEditAction&)
{
    if (bLoading || GIsTransacting)
    {
        return;
    }
    SynchronizeGraph();
}
void FVoxelLandGraphToolkit::SynchronizeGraph()
{
    const uint64 Previous = Revision;
    FString Error;
    FVoxelLandGraphDiagnostic Diagnostic;
    if (!CaptureMirror(Error, &Diagnostic))
    {
        ErrorDiagnostic = MoveTemp(Diagnostic);
        CancelPreview();
        CompileDue = 0.0;
        ++Revision;
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
        return;
    }
    if (Previous != Revision)
    {
        SemanticChangeTime = FPlatformTime::Seconds();
        ErrorDiagnostic = FVoxelLandGraphDiagnostic();
        CancelPreview();
        CompileDue = FPlatformTime::Seconds() + 0.2;
    }
}
void FVoxelLandGraphToolkit::PropertiesChanged(const FPropertyChangedEvent&)
{
    if (PreviewDetails)
    {
        PreviewDetails->ForceRefresh();
    }
    GraphChanged(FEdGraphEditAction());
}
void FVoxelLandGraphToolkit::SelectionChanged(const TSet<UObject*>& Selection)
{
    if (Selection.Num() == 1)
    {
        Details->SetObject(*Selection.CreateConstIterator());
    }
    else
    {
        Details->SetObject(Asset);
    }
}
void FVoxelLandGraphToolkit::RequestCompile()
{
    if (bStopped || bPIE || bReloadPaused)
    {
        return;
    }
    CompileDue = 0.0;
    if (SemanticChangeTime <= 0.0)
    {
        SemanticChangeTime = FPlatformTime::Seconds();
    }
    CancelPreview();
    if (CompileWork)
    {
        bCompileRequested = true;
        Status = TEXT("Compiling; prior preview is stale.");
        return;
    }
    FString Error;
    FVoxelLandGraphDiagnostic Diagnostic;
    if (!CaptureMirror(Error, &Diagnostic))
    {
        ErrorDiagnostic = MoveTemp(Diagnostic);
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
        return;
    }
    FVoxelLandGraphSnapshot Snapshot;
    if (!Asset->CaptureSnapshot(Snapshot, Diagnostic))
    {
        ErrorDiagnostic = MoveTemp(Diagnostic);
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *ErrorDiagnostic.Message);
        return;
    }
    ErrorDiagnostic = FVoxelLandGraphDiagnostic();
    Status = TEXT("Compiling; prior preview is stale.");
    const TSharedRef<FVoxelLandCompileWork, ESPMode::ThreadSafe> Work = MakeShared<FVoxelLandCompileWork, ESPMode::ThreadSafe>();
    Work->Revision = Revision;
    CompileWork = Work;
    WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Work]() mutable
    {
        const double StartTime = FPlatformTime::Seconds();
        const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
        FVoxelLandProgramDraft Draft;
        Work->bSuccess = FVoxelLandGraphCompiler::Compile(Snapshot, Providers, Draft, Work->Diagnostic);
        if (!Work->bSuccess)
        {
            Work->Error = Work->Diagnostic.Message;
        }
        if (Work->bSuccess)
        {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Providers, Program);
            if (Validation)
            {
                Work->Error = Validation.Message;
                Work->bSuccess = false;
            }
            else
            {
                Work->bSuccess = FVoxelLandProgramCodec::Pack(Snapshot, *Program, Work->Payload, Work->Error);
            }
        }
        Work->CompileMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Work->Done.Store(true);
    }));
}
void FVoxelLandGraphToolkit::BindCommands()
{
    FGraphEditorCommands::Register();
    Commands = MakeShared<FUICommandList>();
    Commands->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::DeleteSelected));
    Commands->MapAction(FGenericCommands::Get().Cut, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CutSelected));
    Commands->MapAction(FGraphEditorCommands::Get().CreateComment, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CreateComment));
    Commands->MapAction(FGenericCommands::Get().Copy, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CopySelected));
    Commands->MapAction(FGenericCommands::Get().Paste, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::PasteNodes));
    Commands->MapAction(FGenericCommands::Get().Duplicate, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::DuplicateSelected));
}
void FVoxelLandGraphToolkit::DeleteSelected()
{
    if (!GraphWidget)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("Delete", "Delete graph nodes"));
    Graph->Modify();
    const FGraphPanelSelectionSet Selection = GraphWidget->GetSelectedNodes();
    GraphWidget->ClearSelectionSet();
    for (UObject* Object : Selection)
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
        if (Node && Node->CanUserDeleteNode())
        {
            Node->Modify();
            Node->DestroyNode();
        }
    }
    Graph->NotifyGraphChanged();
}
void FVoxelLandGraphToolkit::CopySelected()
{
    if (!GraphWidget)
    {
        return;
    }
    FGraphPanelSelectionSet Copyable;
    for (UObject* Object : GraphWidget->GetSelectedNodes())
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
        if (Node && Node->CanDuplicateNode())
        {
            Copyable.Add(Node);
        }
    }
    if (!Copyable.IsEmpty())
    {
        FString Exported;
        FEdGraphUtilities::ExportNodesToText(Copyable, Exported);
        FPlatformApplicationMisc::ClipboardCopy(*Exported);
    }
}
void FVoxelLandGraphToolkit::CutSelected()
{
    if (!GraphWidget || !Graph)
    {
        return;
    }
    FGraphPanelSelectionSet Cuttable;
    for (UObject* Object : GraphWidget->GetSelectedNodes())
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
        if (Node && Node->CanUserDeleteNode() && Node->CanDuplicateNode())
        {
            Cuttable.Add(Node);
        }
    }
    if (Cuttable.IsEmpty())
    {
        return;
    }
    FString Exported;
    FEdGraphUtilities::ExportNodesToText(Cuttable, Exported);
    if (Exported.IsEmpty())
    {
        return;
    }
    FPlatformApplicationMisc::ClipboardCopy(*Exported);
    const FScopedTransaction Transaction(LOCTEXT("Cut", "Cut graph nodes"));
    Graph->Modify();
    for (UObject* Object : Cuttable)
    {
        UEdGraphNode* Node = CastChecked<UEdGraphNode>(Object);
        GraphWidget->SetNodeSelection(Node, false);
        Node->Modify();
        Node->DestroyNode();
    }
    Graph->NotifyGraphChanged();
}
void FVoxelLandGraphToolkit::CreateComment()
{
    if (!GraphWidget || !Graph)
    {
        return;
    }
    UEdGraphNode_Comment* Template = NewObject<UEdGraphNode_Comment>();
    FSlateRect Bounds;
    FVector2f Location = GraphWidget->GetPasteLocation2f();
    if (GraphWidget->GetBoundsForSelectedNodes(Bounds, 50.0f))
    {
        Template->SetBounds(Bounds);
        Location = FVector2f(Template->NodePosX, Template->NodePosY);
    }
    FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(Graph, Template, Location);
}
void FVoxelLandGraphToolkit::FrameSelection()
{
    if (GraphWidget)
    {
        GraphWidget->ZoomToFit(true);
    }
}
void FVoxelLandGraphToolkit::ExtendGraphToolbar()
{
    TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
    const FUIAction CompileAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::RequestCompile));
    const FUIAction FrameAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::FrameSelection));
    Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateLambda([CompileAction, FrameAction](FToolBarBuilder& Builder)
        {
            Builder.AddToolBarButton(CompileAction, NAME_None, LOCTEXT("ToolbarCompile", "Compile"),
                LOCTEXT("ToolbarCompileTip", "Compile the terrain graph."), FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.CompileStatus.Background"));
            Builder.AddToolBarButton(FrameAction, NAME_None, LOCTEXT("ToolbarFrame", "Frame selection"),
                LOCTEXT("ToolbarFrameTip", "Frame selected graph nodes."), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FrameActor"));
        }));
    AddToolbarExtender(Extender);
}
void FVoxelLandGraphToolkit::PasteNodes()
{
    if (!GraphWidget)
    {
        return;
    }
    FString Text;
    FPlatformApplicationMisc::ClipboardPaste(Text);
    if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("Paste", "Paste graph nodes"));
    Graph->Modify();
    GraphWidget->ClearSelectionSet();
    TSet<UEdGraphNode*> Imported;
    FEdGraphUtilities::ImportNodesFromText(Graph, Text, Imported);
    for (UEdGraphNode* Node : Imported)
    {
        Node->SetFlags(RF_Transactional);
        Node->Modify();
        Node->CreateNewGuid();
        // ImportNodesFromText resolves internal pin links before returning. Reassign
        // IDs only after that pass; links themselves are retained as pin pointers.
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin)
            {
                Pin->Modify();
                Pin->PinId = FGuid::NewGuid();
            }
        }
        Node->NodePosX += 32;
        Node->NodePosY += 32;
        GraphWidget->SetNodeSelection(Node, true);
    }
    Graph->NotifyGraphChanged();
}
void FVoxelLandGraphToolkit::DuplicateSelected()
{
    if (!GraphWidget)
    {
        return;
    }
    for (UObject* Object : GraphWidget->GetSelectedNodes())
    {
        const UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
        if (Node && Node->CanDuplicateNode())
        {
            CopySelected();
            PasteNodes();
            return;
        }
    }
}



void FVoxelLandGraphToolkit::CancelPreview()
{
    PreviewFence.Reset();
    ++PreviewRevision;
    bPreviewRequested = false;
    if (PreviewCancel)
    {
        PreviewCancel->Store(true);
        PreviewCancel.Reset();
    }
}
void FVoxelLandGraphToolkit::BeginPIE(bool)
{
    bPIE = true;
    ++Revision;
    CancelPreview();
    CompileDue = 0.0;
    bCompileRequested = false;
    for (const TFuture<void>& Task : WorkerTasks)
    {
        Task.Wait();
    }
    WorkerTasks.Reset();
    CompileWork.Reset();
    PreviewWork.Reset();
    Status = TEXT("Stale: preview paused during PIE.");
}
void FVoxelLandGraphToolkit::EndPIE(bool)
{
    bPIE = false;
    RequestCompile();
}
void FVoxelLandGraphToolkit::ClearPreview()
{
    if (PreviewViewport)
    {
        PreviewViewport->SetTerrainBounds(FBox(ForceInit));
    }
    if (PreviewScene)
    {
        for (UVoxelLandChunkComponent* Component : PreviewComponents)
        {
            if (Component)
            {
                PreviewScene->RemoveComponent(Component);
            }
        }
    }
    PreviewComponents.Reset();
}
void FVoxelLandGraphToolkit::SeedCommitted(const FText& Text, ETextCommit::Type)
{
    int64 NewSeed = 0;
    if (!LexTryParseString(NewSeed, *Text.ToString()))
    {
        Status = TEXT("Error: seed must be a signed 64-bit integer; preview is stale.");
        CancelPreview();
        return;
    }
    if (NewSeed != PreviewSeed)
    {
        const FScopedTransaction Transaction(LOCTEXT("SetPreviewSeed", "Set preview seed"));
        PreviewSettings->Modify();
        PreviewSettings->Seed = NewSeed;
        SynchronizePreviewSettings(true);
    }
}
void FVoxelLandGraphToolkit::CommitPreviewRadius(int32 Value, bool bHorizontal)
{
    if (!PreviewSettings)
    {
        return;
    }
    const int32 Radius = FMath::Clamp(Value, 1, 2);
    int32& Target = bHorizontal ? PreviewSettings->HorizontalRadius : PreviewSettings->VerticalRadius;
    if (Target == Radius)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("SetPreviewRadius", "Set preview radius"));
    PreviewSettings->Modify();
    Target = Radius;
    SynchronizePreviewSettings(true);
}
void FVoxelLandGraphToolkit::PauseForReload(bool bGraphAffected)
{
    if (bStopped) { return; }
    bReloadGraphAffected |= bGraphAffected;
    if (bReloadPaused) { return; }
    bReloadPaused = true;
    StatusBeforeReload = Status;
    bReloadGraphAffected |= CompileWork.IsValid() || bCompileRequested || CompileDue > 0.0;
    bResumePreviewAfterReload = PreviewWork.IsValid() || bPreviewRequested;
    ++Revision;
    CancelPreview();
    CompileDue = 0.0;
    bCompileRequested = false;
#if WITH_DEV_AUTOMATION_TESTS
    if (ReloadDrainingForTests)
    {
        ReloadDrainingForTests();
    }
#endif
    for (const TFuture<void>& Task : WorkerTasks) { Task.Wait(); }
    WorkerTasks.Reset();
    CompileWork.Reset();
    PreviewWork.Reset();
    Status = TEXT("Stale: asset reload in progress.");
}
void FVoxelLandGraphToolkit::ResumeAfterReload()
{
    if (bStopped || !bReloadPaused) { return; }
    bReloadPaused = false;
    const bool bCompile = bReloadGraphAffected;
    const bool bPreview = bResumePreviewAfterReload;
    bReloadGraphAffected = false;
    bResumePreviewAfterReload = false;
    SynchronizePreviewSettings(true);
    if (bCompile) { RequestCompile(); }
    else if (bPreview) { StartPreview(); }
    else { Status = StatusBeforeReload; }
    StatusBeforeReload.Reset();
}
void FVoxelLandGraphToolkit::PackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
    if (bStopped) { return; }
    if (Phase == EPackageReloadPhase::PrePackageLoad && Event && Event->GetOldPackage())
    {
        const UPackage* Package = Event->GetOldPackage();
        const bool bGraphAffected = Asset && Asset->GetOutermost() == Package;
        const UObject* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.Get() : nullptr;
        const UObject* Selected = SelectedPreviewSettings.Get();
        if (bGraphAffected || (Setup && Setup->GetOutermost() == Package) ||
            (Selected && Selected->GetOutermost() == Package) || (PreviewMaterial && PreviewMaterial->GetOutermost() == Package))
        {
            bPackageReloadInProgress = true;
            PauseForReload(bGraphAffected);
        }
    }
    else if (Phase == EPackageReloadPhase::PrePackageFixup && Event && Asset && Event->GetOldPackage() == Asset->GetOutermost())
    {
        PauseForReload(true);
        Stop();
        if (bHasEditorWindow) { CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed); }
    }
    else if (Phase == EPackageReloadPhase::PostBatchPostGC && bPackageReloadInProgress)
    {
        bPackageReloadInProgress = false;
        ResumeAfterReload();
    }
}
void FVoxelLandGraphToolkit::ObjectsReplaced(const TMap<UObject*, UObject*>& Replacements)
{
    if (bStopped) { return; }
    bool bGraphAffected = Replacements.Contains(Asset) || Replacements.Contains(Graph);
    if (!bGraphAffected && Graph)
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Replacements.Contains(const_cast<UEdGraphNode*>(Node))) { bGraphAffected = true; break; }
        }
    }
    if (bGraphAffected || Replacements.Contains(PreviewSettings))
    {
        PauseForReload(true);
        Stop();
        if (bHasEditorWindow) { CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed); }
        return;
    }
    const UObject* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.Get() : nullptr;
    if ((Setup && Replacements.Contains(const_cast<UObject*>(Setup))) || Replacements.Contains(PreviewMaterial) ||
        (SelectedPreviewSettings.IsValid() && Replacements.Contains(SelectedPreviewSettings.Get())))
    {
        PauseForReload(false);
    }
}
void FVoxelLandGraphToolkit::ObjectsReinstanced(const TMap<UObject*, UObject*>&)
{
    if (!bPackageReloadInProgress)
    {
        ResumeAfterReload();
    }
}
void FVoxelLandGraphToolkit::MaterialSetupPropertyChanged(UObject* Object, FPropertyChangedEvent&)
{
    if (!bStopped && !GIsTransacting && PreviewSettings && PreviewSettings->MaterialSetup.Get() == Object)
    {
        SynchronizePreviewSettings(true);
    }
}
void FVoxelLandGraphToolkit::ApplyPreviewMaterial(UMaterialInterface* Material)
{
    SelectedPreviewMaterial = Material;
#if WITH_DEV_AUTOMATION_TESTS
    ++MaterialApplyCountForTests;
#endif
    if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
    {
        PreviewMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        MaterialDiagnostic = TEXT("Missing or unsupported material; using the default surface material.");
    }
    else
    {
        PreviewMaterial = Material;
        MaterialDiagnostic.Reset();
    }
    for (UVoxelLandChunkComponent* Component : PreviewComponents)
    {
        if (Component)
        {
            Component->SetMaterial(0, PreviewMaterial);
        }
    }
}
void FVoxelLandGraphToolkit::ApplyMaterialSetup()
{
    const UVoxelLandMaterialSetup* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.LoadSynchronous() : nullptr;
    ApplyPreviewMaterial(Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr);
}
void FVoxelLandGraphToolkit::MaterialChanged(const FAssetData& Material)
{
    ApplyPreviewMaterial(Cast<UMaterialInterface>(Material.GetAsset()));
}
FString FVoxelLandGraphToolkit::MaterialPickerPath() const
{
    return SelectedPreviewMaterial.ToSoftObjectPath().ToString();
}
FString FVoxelLandGraphToolkit::SettingsPickerPath() const
{
    return SelectedPreviewSettings.ToSoftObjectPath().ToString();
}
void FVoxelLandGraphToolkit::SettingsChanged(const FAssetData& Settings)
{
    UVoxelLandWorldSettings* Source = Cast<UVoxelLandWorldSettings>(Settings.GetAsset());
    if (Settings.IsValid() && !Source)
    {
        return;
    }
    SelectedPreviewSettings = Source;
    if (Source)
    {
        PreviewSettings = DuplicateObject<UVoxelLandWorldSettings>(Source, GetTransientPackage());
        PreviewSettings->SetFlags(RF_Transient | RF_Transactional);
    }
    else
    {
        PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
        PreviewSettings->Graph = Asset;
    }
    if (PreviewDetails)
    {
        PreviewDetails->SetObject(PreviewSettings, true);
    }
    SynchronizePreviewSettings(true);
    // Picking settings is an explicit material selection even if its setup path
    // matches the prior one (the user may have selected a direct material).
    ApplyMaterialSetup();
}
void FVoxelLandGraphToolkit::PreviewPropertiesChanged(const FPropertyChangedEvent&)
{
    SynchronizePreviewSettings(true);
}
void FVoxelLandGraphToolkit::SynchronizePreviewSettings(bool bApplyChanges)
{
    if (!PreviewSettings)
    {
        return;
    }
    FCanonicalWriter Writer;
    Writer.String(GetPathNameSafe(PreviewSettings->Graph));
    Writer.U32(uint32(PreviewSettings->FormatVersion));
    Writer.U32(uint32(uint64(PreviewSettings->Seed)));
    Writer.U32(uint32(uint64(PreviewSettings->Seed) >> 32));
    Writer.Float(PreviewSettings->VoxelSizeCm);
    Writer.U32(uint32(PreviewSettings->HorizontalRadius));
    Writer.U32(uint32(PreviewSettings->VerticalRadius));
    TArray<FVoxelLandParameterOverride> Sorted = PreviewSettings->Overrides;
    Sorted.Sort([](const FVoxelLandParameterOverride& A, const FVoxelLandParameterOverride& B)
    {
        if (A.ParameterId.A != B.ParameterId.A) { return A.ParameterId.A < B.ParameterId.A; }
        if (A.ParameterId.B != B.ParameterId.B) { return A.ParameterId.B < B.ParameterId.B; }
        if (A.ParameterId.C != B.ParameterId.C) { return A.ParameterId.C < B.ParameterId.C; }
        return A.ParameterId.D < B.ParameterId.D;
    });
    Writer.U32(Sorted.Num());
    for (const FVoxelLandParameterOverride& O : Sorted)
    {
        Writer.U32(O.ParameterId.A); Writer.U32(O.ParameterId.B); Writer.U32(O.ParameterId.C); Writer.U32(O.ParameterId.D);
        Writer.U8(uint8(O.Type));
        if (O.Type == EVoxelLandGraphValueType::Float) { Writer.Float(O.FloatValue); }
        else if (O.Type == EVoxelLandGraphValueType::Int32) { Writer.U32(uint32(O.IntValue)); }
        else { Writer.U8(O.BoolValue ? 1 : 0); }
    }
    FCanonicalWriter MaterialWriter;
    MaterialWriter.String(PreviewSettings->MaterialSetup.ToSoftObjectPath().ToString());
    const UVoxelLandMaterialSetup* Setup = PreviewSettings->MaterialSetup.LoadSynchronous();
    MaterialWriter.U8(Setup ? 1 : 0);
    if (Setup)
    {
        MaterialWriter.U32(uint32(Setup->FormatVersion));
        MaterialWriter.String(Setup->Material.ToSoftObjectPath().ToString());
    }
    const bool bDensityChanged = bHasPreviewSettingsSnapshot && PreviewDensitySnapshot != Writer.Bytes;
    const bool bMaterialChanged = bHasPreviewSettingsSnapshot && PreviewMaterialSnapshot != MaterialWriter.Bytes;
    PreviewDensitySnapshot = MoveTemp(Writer.Bytes);
    PreviewMaterialSnapshot = MoveTemp(MaterialWriter.Bytes);
    bHasPreviewSettingsSnapshot = true;
    PreviewSeed = PreviewSettings->Seed;
    PreviewHorizontalRadius = FMath::Clamp(PreviewSettings->HorizontalRadius, 1, 2);
    PreviewVerticalRadius = FMath::Clamp(PreviewSettings->VerticalRadius, 1, 2);
    if (bApplyChanges && bMaterialChanged)
    {
        ApplyMaterialSetup();
    }
    if (bApplyChanges && bDensityChanged)
    {
        SemanticChangeTime = FPlatformTime::Seconds();
        StartPreview();
    }
}
FReply FVoxelLandGraphToolkit::ResetPreviewCamera()
{
    if (PreviewViewport)
    {
        PreviewViewport->ResetCamera();
    }
    return FReply::Handled();
}
FReply FVoxelLandGraphToolkit::FramePreviewTerrain()
{
    if (!bStopped && PreviewViewport)
    {
        PreviewViewport->FrameTerrain();
    }
    return FReply::Handled();
}
bool FVoxelLandGraphToolkit::BuildPreviewProfile(FVoxelLandZoneProfile& Profile, FString& Error) const
{
    if (!PreviewSettings || PreviewSettings->Graph != Asset || !PreviewSettings->ValidateSettings(Error))
    {
        Error = FString::Printf(TEXT("Settings must reference this graph and be valid. %s"), *Error);
        return false;
    }
    Profile.VoxelSizeCm = PreviewSettings->VoxelSizeCm;
    for (const FVoxelLandParameterOverride& Override : PreviewSettings->Overrides)
    {
        const FVoxelLandSerializedParameter* Parameter = Asset->CompiledProgram.Parameters.FindByPredicate([&Override](const FVoxelLandSerializedParameter& Candidate)
        {
            return Candidate.Id == Override.ParameterId;
        });
        if (!Parameter)
        {
            // Unreachable declarations are valid settings but have no VM slot.
            continue;
        }
        if (Override.Type == EVoxelLandGraphValueType::Float)
        {
            Profile.FloatOverrides.Add(Parameter->Name, Override.FloatValue);
        }
        else if (Override.Type == EVoxelLandGraphValueType::Int32)
        {
            Profile.IntOverrides.Add(Parameter->Name, Override.IntValue);
        }
        else
        {
            Profile.BoolOverrides.Add(Parameter->Name, Override.BoolValue);
        }
    }
    return true;
}
void FVoxelLandGraphToolkit::StartPreview()
{
    if (bStopped || bPIE || bReloadPaused)
    {
        return;
    }
    CancelPreview();
    if (SemanticChangeTime <= 0.0)
    {
        SemanticChangeTime = FPlatformTime::Seconds();
    }
    if (PreviewWork)
    {
        bPreviewRequested = true;
        Status = TEXT("Generating; previous preview is stale.");
        return;
    }
    FString Error;
    FVoxelLandGraphSnapshot Snapshot;
    if (!Asset->CaptureSnapshot(Snapshot, Error) || Asset->CompiledProgram.ProgramVersion == 0 ||
        uint64(Asset->CompiledProgram.SourceFingerprint) != FVoxelLandProgramCodec::SourceFingerprint(Snapshot))
    {
        Status = FString::Printf(TEXT("Stale preview: compiled source unavailable or invalid. %s"), *Error);
        return;
    }
    FVoxelLandZoneProfile Profile;
    if (!BuildPreviewProfile(Profile, Error))
    {
        Status = FString::Printf(TEXT("Error; preview is stale: %s"), *Error);
        return;
    }
    const FVoxelLandSerializedProgram Payload = Asset->CompiledProgram;
    const int64 Seed = PreviewSeed;
    const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Cancel = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
    PreviewCancel = Cancel;
    const int32 HorizontalRadius = PreviewHorizontalRadius;
    const int32 VerticalRadius = PreviewVerticalRadius;
    Status = TEXT("Generating bounded chunks; previous preview is stale.");
    const TSharedRef<FVoxelLandPreviewWork, ESPMode::ThreadSafe> Work = MakeShared<FVoxelLandPreviewWork, ESPMode::ThreadSafe>();
    Work->PreviewRevision = PreviewRevision;
    Work->SourceRevision = Revision;
#if WITH_DEV_AUTOMATION_TESTS
    Work->SeedForTests = Seed;
#endif
    Work->VoxelSize = Profile.VoxelSizeCm;
    Work->ChunkCount = (2 * HorizontalRadius + 1) * (2 * HorizontalRadius + 1) * 2 * VerticalRadius;
    PreviewWork = Work;
#if WITH_DEV_AUTOMATION_TESTS
    ++PreviewStartCountForTests;
#endif
#if WITH_DEV_AUTOMATION_TESTS
    const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(PreviewReloadBarrierForTests);
    WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Payload, Profile = MoveTemp(Profile), Seed, HorizontalRadius, VerticalRadius, Cancel, Work, Barrier]() mutable
#else
    WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Payload, Profile = MoveTemp(Profile), Seed, HorizontalRadius, VerticalRadius, Cancel, Work]() mutable
#endif
    {
        const double StartTime = FPlatformTime::Seconds();
        Work->bSuccess = FVoxelLandPreviewJob::Run(Snapshot, Payload, Seed, Profile, HorizontalRadius, VerticalRadius, Cancel, Work->Meshes, Work->Error
#if WITH_DEV_AUTOMATION_TESTS
            , Barrier
#endif
        );
        Work->GenerationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Work->Done.Store(true);
    }));

}
bool FVoxelLandGraphToolkit::PublishPreview(const FVoxelLandPreviewWork& Finished)
{
    const double UploadStart = FPlatformTime::Seconds();
    TArray<TPair<FIntVector, UVoxelLandChunkComponent*>> Staged;
    for (const auto& Entry : Finished.Meshes)
    {
        UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        Component->SetMaterial(0, PreviewMaterial);
        if (!Component->SetMesh(Entry.Value))
        {
            Status = TEXT("Error; previous preview is stale: a preview mesh was rejected.");
            return false;
        }
        Staged.Emplace(Entry.Key, Component);
    }
    ClearPreview();
    const bool bGPURequested = CVarVoxelLandPreviewGPUUploadTiming.GetValueOnGameThread() != 0;
    TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> UploadObservation;
    if (bGPURequested && !Staged.IsEmpty() && FVoxelLandPreviewUploadObservation::IsSupported())
    {
        UploadObservation = MakeShared<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe>(Staged.Num());
    }
    FBox TerrainBounds(ForceInit);
    for (const auto& Entry : Staged)
    {
        Entry.Value->SetPreviewUploadObservation(UploadObservation);
        PreviewScene->AddComponent(Entry.Value, FTransform(VoxelLandMeshCoordinates::RenderOrigin(Entry.Key, Finished.VoxelSize)));
        PreviewComponents.Add(Entry.Value);
        TerrainBounds += Entry.Value->Bounds.GetBox();
    }
    if (PreviewViewport)
    {
        PreviewViewport->SetTerrainBounds(TerrainBounds);
    }
#if WITH_DEV_AUTOMATION_TESTS
    ++PreviewPublishCountForTests;
    PublishedSeedForTests = Finished.SeedForTests;
#endif
    const double SetupCompleted = FPlatformTime::Seconds();
    PreviewFence = MakeShared<FVoxelLandPreviewFence>();
    PreviewFence->PreviewRevision = Finished.PreviewRevision;
    PreviewFence->SourceRevision = Finished.SourceRevision;
    PreviewFence->NonemptyChunks = PreviewComponents.Num();
    PreviewFence->ChunkCount = Finished.ChunkCount;
    PreviewFence->SemanticStart = SemanticChangeTime > 0.0 ? SemanticChangeTime : UploadStart;
    PreviewFence->SetupCompleted = SetupCompleted;
    PreviewFence->CompileMs = LastCompileMs;
    PreviewFence->GenerationMs = Finished.GenerationMs;
    PreviewFence->SetupMs = (SetupCompleted - UploadStart) * 1000.0;
    PreviewFence->bGPURequested = bGPURequested;
    PreviewFence->UploadObservation = MoveTemp(UploadObservation);
    PreviewFence->Fence.BeginFence(FRenderCommandFence::ESyncDepth::RenderThread);
    Status = TEXT("Preview submitted; waiting for render-thread commands.");
    if (PreviewViewport)
    {
        PreviewViewport->Invalidate();
    }
    return true;
}
void FVoxelLandGraphToolkit::PollPreviewFence()
{
    if (!PreviewFence)
    {
        return;
    }
    const TSharedPtr<FVoxelLandPreviewFence> Finished = PreviewFence;
    if (bStopped || bPIE || bReloadPaused || Finished->PreviewRevision != PreviewRevision || Finished->SourceRevision != Revision)
    {
        PreviewFence.Reset();
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (!Finished->bRenderComplete)
    {
        if (!Finished->Fence.IsFenceComplete())
        {
            return;
        }
        Finished->bRenderComplete = true;
        Finished->ReadyStatus = FString::Printf(TEXT("Ready: render commands complete: %d nonempty / %d chunks; latest compile CPU %.1f ms, generation CPU %.1f ms, game-thread setup %.1f ms, render-thread fence observed after %.1f ms, change-to-fence observed %.1f ms."),
            Finished->NonemptyChunks, Finished->ChunkCount, Finished->CompileMs, Finished->GenerationMs, Finished->SetupMs,
            (Now - Finished->SetupCompleted) * 1000.0, (Now - Finished->SemanticStart) * 1000.0);
        SemanticChangeTime = 0.0;
    }
    if (!Finished->bGPURequested)
    {
        Status = Finished->ReadyStatus + TEXT(" GPU completion not measured.");
        PreviewFence.Reset();
    }
    else if (!Finished->UploadObservation)
    {
        Status = Finished->ReadyStatus + TEXT(" GPU upload observation unavailable (unsupported RHI or empty preview); presentation not measured.");
        PreviewFence.Reset();
    }
    else if (
#if WITH_DEV_AUTOMATION_TESTS
        CVarVoxelLandPreviewGPUUploadHoldForTests.GetValueOnGameThread() == 0 &&
#endif
        Finished->UploadObservation->IsComplete())
    {
        Status = Finished->ReadyStatus + FString::Printf(TEXT(" Initial GPU buffer uploads observed complete %.1f ms after setup, %.1f ms after change (upper bounds, include polling); copy duration and presentation not measured."),
            (Now - Finished->SetupCompleted) * 1000.0, (Now - Finished->SemanticStart) * 1000.0);
        PreviewFence.Reset();
    }
    else if (Now - Finished->SetupCompleted >= 10.0)
    {
        Status = Finished->ReadyStatus + TEXT(" GPU upload observation unavailable (timed out); presentation not measured.");
        PreviewFence.Reset();
    }
    else
    {
        Status = Finished->ReadyStatus + TEXT(" GPU upload observation pending; presentation not measured.");
    }
}
void FVoxelLandGraphToolkit::ProcessCompileWork()
{
    if (!CompileWork || !CompileWork->Done.Load())
    {
        return;
    }
    const TSharedPtr<FVoxelLandCompileWork, ESPMode::ThreadSafe> Finished = MoveTemp(CompileWork);
    if (!bPIE && Finished->Revision == Revision && !bCompileRequested)
    {
        if (Finished->bSuccess)
        {
            ErrorDiagnostic = FVoxelLandGraphDiagnostic();
            StoreCompiledProgram(*Asset, MoveTemp(Finished->Payload));
            LastCompileMs = Finished->CompileMs;
            StartPreview();
        }
        else
        {
            ErrorDiagnostic = Finished->Diagnostic;
            Status = FString::Printf(TEXT("Error; preview stale: %s"), *Finished->Error);
        }
    }
    if (bCompileRequested && !bPIE)
    {
        bCompileRequested = false;
        RequestCompile();
    }
}
void FVoxelLandGraphToolkit::ProcessPreviewWork()
{
    if (!PreviewWork || !PreviewWork->Done.Load())
    {
        return;
    }
    const TSharedPtr<FVoxelLandPreviewWork, ESPMode::ThreadSafe> Finished = MoveTemp(PreviewWork);
    if (!bPIE && Finished->PreviewRevision == PreviewRevision && Finished->SourceRevision == Revision && !bPreviewRequested)
    {
        PreviewCancel.Reset();
        if (!Finished->bSuccess)
        {
            Status = FString::Printf(TEXT("Error; previous preview is stale: %s"), *Finished->Error);
        }
        else
        {
            PublishPreview(*Finished);
        }
    }
    if (bPreviewRequested && !bPIE)
    {
        bPreviewRequested = false;
        StartPreview();
    }
}
bool FVoxelLandGraphToolkit::PollJobs(float)
{
    if (bStopped)
    {
        return false;
    }
    WorkerTasks.RemoveAll([](const TFuture<void>& Task) { return Task.IsReady(); });
    const uint64 CurrentPresentationRevision = FVoxelLandEditorOperatorRegistry::GetRevision();
    if (PaletteWidget && PresentationRevision != CurrentPresentationRevision)
    {
        PresentationRevision = CurrentPresentationRevision;
        PaletteWidget->RefreshAllActions(true, false);
    }
    ProcessCompileWork();
    ProcessPreviewWork();
    PollPreviewFence();
    if (!bPIE && CompileDue > 0.0 && FPlatformTime::Seconds() >= CompileDue)
    {
        RequestCompile();
    }
    return true;
}
#undef LOCTEXT_NAMESPACE
