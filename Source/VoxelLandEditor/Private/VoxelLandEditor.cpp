#include "VoxelLandEditor.h"
#include "AssetToolsModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetTypeActions_Base.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandGenerator.h"
#include "Async/Async.h"
#include "UObject/ObjectSaveContext.h"
#include "VoxelLandAlgo.h"
#include "Operators/VoxelLandSquareOperator.h"

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
            FVoxelLandProgramDraft Draft;
            if (!FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error))
            {
                return false;
            }
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Program);
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
