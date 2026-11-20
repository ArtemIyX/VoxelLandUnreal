#include "VoxelLandEditor.h"
#include "Operators/VoxelLandOperator.h"
#include "AssetToolsModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetTypeActions_Base.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandGraphPinFactory.h"
#include "Graph/VoxelLandWorldSettingsDetails.h"
#include "PropertyEditorModule.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandGenerator.h"
#include "Async/Async.h"
#include "UObject/ObjectSaveContext.h"
#include "VoxelLandAlgo.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Operators/VoxelLandExampleFilterOperator.h"

#define LOCTEXT_NAMESPACE "FVoxelLandEditorModule"
namespace
{
class FVoxelLandGraphAssetActions : public FAssetTypeActions_Base
{
public:
    virtual FText GetName() const override { return LOCTEXT("GraphAsset", "VoxelLand Terrain Graph"); }
    virtual FColor GetTypeColor() const override { return FColor(67, 158, 92); }
    virtual UClass* GetSupportedClass() const override { return UVoxelLandTerrainGraph::StaticClass(); }
    virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }
    virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host) override
    {
        for (UObject* Object : Objects)
        {
            if (UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Object))
            {
                TSharedRef<FVoxelLandGraphToolkit> Editor = MakeShared<FVoxelLandGraphToolkit>();
                Editor->Open(Graph, Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host);
                Editors.Add(Editor);
            }
        }
    }
    void StopEditors()
    {
        for (const TWeakPtr<FVoxelLandGraphToolkit>& Weak : Editors)
        {
            if (TSharedPtr<FVoxelLandGraphToolkit> Editor = Weak.Pin())
            {
                Editor->Stop();
                if (!Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed))
                {
                    UE_LOG(LogTemp, Warning, TEXT("VoxelLand graph editor declined to close during module shutdown"));
                }
            }
        }
        Editors.Reset();
    }
private:
    TArray<TWeakPtr<FVoxelLandGraphToolkit>> Editors;
};
}
void FVoxelLandEditorModule::StartupModule()
{
    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).RegisterCustomClassLayout(
        TEXT("VoxelLandWorldSettings"), FOnGetDetailCustomizationInstance::CreateStatic(&FVoxelLandWorldSettingsDetails::MakeInstance));
    PinFactory = MakeShared<FVoxelLandGraphPinFactory>();
    FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
    FModuleManager::LoadModuleChecked<FVoxelLandAlgoModule>(TEXT("VoxelLandAlgo"));
    FVoxelLandEditorOperatorPresentation Square;
    Square.OperatorId = VoxelLandSquareOperatorId();
    Square.Category = LOCTEXT("Extensions", "C++ extensions");
    Square.DisplayName = LOCTEXT("Square", "Square");
    Square.Tooltip = LOCTEXT("SquareTooltip", "Example registered scalar C++ operator.");
    FVoxelLandGraphPinRecord Input;
    Input.Name = TEXT("Value");
    Input.Type = EVoxelLandGraphValueType::Float;
    Square.Inputs.Add(MoveTemp(Input));
    FString Error;
    if (!FVoxelLandEditorOperatorRegistry::Register(Square, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelLand square editor presentation unavailable: %s"), *Error);
    }
    FVoxelLandEditorOperatorPresentation Filter;
    Filter.OperatorId = VoxelLandExampleFilterOperatorId();
    Filter.Category = LOCTEXT("FilterExtensions", "C++ filters");
    Filter.DisplayName = LOCTEXT("ExampleFilter", "Example X Average Filter");
    Filter.Tooltip = LOCTEXT("ExampleFilterTooltip", "One-cell quantized three-sample average along X; connect directly to Density Output.");
    FVoxelLandGraphPinRecord Density;
    Density.Name = TEXT("Density");
    Density.Type = EVoxelLandGraphValueType::Float;
    Filter.Inputs.Add(MoveTemp(Density));
    if (!FVoxelLandEditorOperatorRegistry::Register(Filter, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelLand filter presentation unavailable: %s"), *Error);
    }
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    GraphAssetActions = MakeShared<FVoxelLandGraphAssetActions>();
    AssetTools.RegisterAssetTypeActions(GraphAssetActions.ToSharedRef());
    PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FVoxelLandEditorModule::OnObjectPreSave);
}
void FVoxelLandEditorModule::ShutdownModule()
{
    FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
    if (GraphAssetActions.IsValid())
    {
        StaticCastSharedPtr<FVoxelLandGraphAssetActions>(GraphAssetActions)->StopEditors();
    }
    if (GraphAssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
    {
        FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(GraphAssetActions.ToSharedRef());
    }
    GraphAssetActions.Reset();
    if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
    {
        FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).UnregisterCustomClassLayout(TEXT("VoxelLandWorldSettings"));
    }
    FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
    PinFactory.Reset();
    FVoxelLandEditorOperatorRegistry::Unregister(VoxelLandExampleFilterOperatorId(), 1);
    FVoxelLandEditorOperatorRegistry::Unregister(VoxelLandSquareOperatorId(), 1);
    FVoxelLandEditorOperatorRegistry::Clear();
}
void FVoxelLandEditorModule::OnObjectPreSave(UObject* Object, FObjectPreSaveContext Context)
{
    UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Object);
    if (!Graph || !Context.IsFirstConcurrentSave())
    {
        return;
    }
    FString Error;
    FVoxelLandGraphSnapshot Snapshot;
    bool bOk = Graph->CaptureSnapshot(Snapshot, Error);
    FVoxelLandSerializedProgram Payload;
    if (bOk)
    {
        TFuture<bool> Work = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]()
        {
            const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
            FVoxelLandProgramDraft Draft;
            if (!FVoxelLandGraphCompiler::Compile(Snapshot, Providers, Draft, Error))
            {
                return false;
            }
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Providers, Program);
            if (Validation)
            {
                Error = Validation.Message;
                return false;
            }
            return FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
        });
        bOk = Work.Get();
    }
    if (bOk)
    {
        Graph->CompiledProgram = MoveTemp(Payload);
    }
    else if (Context.IsCooking())
    {
        Graph->CompiledProgram = FVoxelLandSerializedProgram();
        UE_LOG(LogTemp, Error, TEXT("VoxelLand graph %s cannot cook: %s"), *Graph->GetPathName(), *Error);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelLand graph %s is not compiled: %s"), *Graph->GetPathName(), *Error);
    }
}
#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FVoxelLandEditorModule, VoxelLandEditor)
