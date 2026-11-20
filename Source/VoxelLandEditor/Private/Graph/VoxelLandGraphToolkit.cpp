#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandPreviewViewport.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandMeshing.h"
#include "Rendering/VoxelLandChunkComponent.h"
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
        Pin->DefaultValue = FString::SanitizeFloat(Record.FloatDefault);
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
    TArray<FVoxelLandGraphLinkRecord>& Links, bool& bValidDefaults, FString& Error)
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
            Error = FString::Printf(TEXT("Invalid default for pin %s."), *Pin->PinName.ToString());
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
        const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel, FResult& Meshes, FString& Error)
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
    FVoxelLandPreviewJob::FResult Meshes;
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
void FVoxelLandGraphToolkit::Open(UVoxelLandTerrainGraph* InAsset, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host)
{
    Asset = InAsset;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    PreviewScene->SetFloorVisibility(false, true);
    PreviewViewport = SNew(SVoxelLandPreviewViewport).Scene(PreviewScene);
    PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewSettings->Graph = Asset;
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
    RegenerateMenusAndToolbars();
    RequestCompile();
}
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
            if (GraphWidget)
            {
                GraphWidget->ZoomToFit(true);
            }
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
FReply FVoxelLandGraphToolkit::FocusError()
{
    if (GraphWidget && ErrorNode.IsValid())
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == ErrorNode)
            {
                GraphWidget->JumpToNode(Node);
                break;
            }
        }
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
            + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ResetCamera", "Reset camera")).OnClicked_Raw(this, &FVoxelLandGraphToolkit::ResetPreviewCamera)]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(UMaterialInterface::StaticClass()).OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::MaterialChanged)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(UVoxelLandWorldSettings::StaticClass()).OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::SettingsChanged)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Radius", "Preview radius (XY / Z)"))]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(2).Value_Lambda([this] { return PreviewHorizontalRadius; }).OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { PreviewHorizontalRadius = FMath::Clamp(Value, 1, 2); StartPreview(); })]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(2).Value_Lambda([this] { return PreviewVerticalRadius; }).OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { PreviewVerticalRadius = FMath::Clamp(Value, 1, 2); StartPreview(); })]]
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

bool FVoxelLandGraphToolkit::CaptureMirror(FString& Error)
{
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
        if (!CaptureNode(*Node, Nodes, Links, bValidDefaults, Error))
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
    if (NewHash != LastSourceHash)
    {
        Asset->Modify();
        Asset->Nodes = MoveTemp(Nodes);
        Asset->Links = MoveTemp(Links);
        Asset->MarkPackageDirty();
        LastSourceHash = NewHash;
        ++Revision;
        Status = TEXT("Stale: graph changed.");
    }
    return bValidDefaults;
}
void FVoxelLandGraphToolkit::GraphChanged(const FEdGraphEditAction&)
{
    if (bLoading)
    {
        return;
    }
    const uint64 Previous = Revision;
    FString Error;
    if (!CaptureMirror(Error))
    {
        CancelPreview();
        ++Revision;
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
        return;
    }
    if (Previous != Revision)
    {
        CancelPreview();
        CompileDue = FPlatformTime::Seconds() + 0.2;
    }
}
void FVoxelLandGraphToolkit::PropertiesChanged(const FPropertyChangedEvent&)
{
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
    if (bStopped || bPIE)
    {
        return;
    }
    CompileDue = 0.0;
    CancelPreview();
    if (CompileWork)
    {
        bCompileRequested = true;
        Status = TEXT("Compiling; prior preview is stale.");
        return;
    }
    FString Error;
    if (!CaptureMirror(Error))
    {
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
        return;
    }
    FVoxelLandGraphSnapshot Snapshot;
    if (!Asset->CaptureSnapshot(Snapshot, Error))
    {
        Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
        return;
    }
    Status = TEXT("Compiling; prior preview is stale.");
    const TSharedRef<FVoxelLandCompileWork, ESPMode::ThreadSafe> Work = MakeShared<FVoxelLandCompileWork, ESPMode::ThreadSafe>();
    Work->Revision = Revision;
    CompileWork = Work;
    WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Work]() mutable
    {
        const double StartTime = FPlatformTime::Seconds();
        FVoxelLandProgramDraft Draft;
        Work->bSuccess = FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Work->Error);
        if (Work->bSuccess)
        {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Program);
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
    Commands = MakeShared<FUICommandList>();
    Commands->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::DeleteSelected));
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
    FString Exported;
    FEdGraphUtilities::ExportNodesToText(GraphWidget->GetSelectedNodes(), Exported);
    FPlatformApplicationMisc::ClipboardCopy(*Exported);
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
    CopySelected();
    PasteNodes();
}



void FVoxelLandGraphToolkit::CancelPreview()
{
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
        PreviewSeed = NewSeed;
        PreviewSettings->Seed = NewSeed;
        StartPreview();
    }
}
void FVoxelLandGraphToolkit::ApplyPreviewMaterial(UMaterialInterface* Material)
{
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
void FVoxelLandGraphToolkit::SettingsChanged(const FAssetData& Settings)
{
    const UVoxelLandWorldSettings* Source = Cast<UVoxelLandWorldSettings>(Settings.GetAsset());
    if (!Source)
    {
        return;
    }
    bool bDensityChanged = !PreviewSettings || PreviewSettings->Graph != Source->Graph ||
        PreviewSettings->Seed != Source->Seed || PreviewSettings->VoxelSizeCm != Source->VoxelSizeCm ||
        PreviewSettings->HorizontalRadius != Source->HorizontalRadius || PreviewSettings->VerticalRadius != Source->VerticalRadius ||
        PreviewSettings->Overrides.Num() != Source->Overrides.Num();
    if (!bDensityChanged)
    {
        for (int32 Index = 0; Index < Source->Overrides.Num(); ++Index)
        {
            const FVoxelLandParameterOverride& A = PreviewSettings->Overrides[Index];
            const FVoxelLandParameterOverride& B = Source->Overrides[Index];
            if (A.ParameterId != B.ParameterId || A.Type != B.Type || A.FloatValue != B.FloatValue ||
                A.IntValue != B.IntValue || A.BoolValue != B.BoolValue)
            {
                bDensityChanged = true;
                break;
            }
        }
    }
    PreviewSettings = DuplicateObject<UVoxelLandWorldSettings>(Source, GetTransientPackage());
    PreviewSettings->SetFlags(RF_Transient);
    PreviewSeed = PreviewSettings->Seed;
    PreviewHorizontalRadius = FMath::Clamp(PreviewSettings->HorizontalRadius, 1, 2);
    PreviewVerticalRadius = FMath::Clamp(PreviewSettings->VerticalRadius, 1, 2);
    PreviewDetails->SetObject(PreviewSettings, true);
    ApplyMaterialSetup();
    if (bDensityChanged)
    {
        StartPreview();
    }
}
void FVoxelLandGraphToolkit::PreviewPropertiesChanged(const FPropertyChangedEvent& Event)
{
    if (Event.MemberProperty && Event.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UVoxelLandWorldSettings, MaterialSetup))
    {
        ApplyMaterialSetup();
        return;
    }
    PreviewSeed = PreviewSettings->Seed;
    StartPreview();
}
FReply FVoxelLandGraphToolkit::ResetPreviewCamera()
{
    if (PreviewViewport)
    {
        PreviewViewport->ResetCamera();
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
    if (bStopped || bPIE)
    {
        return;
    }
    CancelPreview();
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
    Work->VoxelSize = Profile.VoxelSizeCm;
    Work->ChunkCount = (2 * HorizontalRadius + 1) * (2 * HorizontalRadius + 1) * 2 * VerticalRadius;
    PreviewWork = Work;
    WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Payload, Profile = MoveTemp(Profile), Seed, HorizontalRadius, VerticalRadius, Cancel, Work]() mutable
    {
        const double StartTime = FPlatformTime::Seconds();
        Work->bSuccess = FVoxelLandPreviewJob::Run(Snapshot, Payload, Seed, Profile, HorizontalRadius, VerticalRadius, Cancel, Work->Meshes, Work->Error);
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
    for (const auto& Entry : Staged)
    {
        PreviewScene->AddComponent(Entry.Value, FTransform(VoxelLandMeshCoordinates::RenderOrigin(Entry.Key, Finished.VoxelSize)));
        PreviewComponents.Add(Entry.Value);
    }
    const double UploadMs = (FPlatformTime::Seconds() - UploadStart) * 1000.0;
    Status = FString::Printf(TEXT("Ready: %d nonempty / %d chunks; compile %.1f ms, generation %.1f ms, game-thread setup %.1f ms. %s"),
        PreviewComponents.Num(), Finished.ChunkCount, LastCompileMs, Finished.GenerationMs, UploadMs, TEXT(""));
    if (PreviewViewport)
    {
        PreviewViewport->Invalidate();
    }
    return true;
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
            ErrorNode.Invalidate();
            Asset->Modify();
            Asset->CompiledProgram = MoveTemp(Finished->Payload);
            Asset->MarkPackageDirty();
            LastCompileMs = Finished->CompileMs;
            StartPreview();
        }
        else
        {
            ErrorNode.Invalidate();
            if (Finished->Error.StartsWith(TEXT("[node=")))
            {
                const int32 End = Finished->Error.Find(TEXT("]"));
                if (End > 6)
                {
                    FGuid::Parse(Finished->Error.Mid(6, End - 6), ErrorNode);
                }
            }
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
    if (!bPIE && CompileDue > 0.0 && FPlatformTime::Seconds() >= CompileDue)
    {
        RequestCompile();
    }
    return true;
}
#undef LOCTEXT_NAMESPACE
