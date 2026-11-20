#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"

class UVoxelLandTerrainGraph;
class UEdGraph;
class IDetailsView;
class SGraphEditor;
class SDockTab;
class FUICommandList;
class FAdvancedPreviewScene;
class SVoxelLandPreviewViewport;
class SGraphActionMenu;
class UVoxelLandChunkComponent;
class UMaterialInterface;
class UVoxelLandWorldSettings;
struct FAssetData;
class FVoxelLandCompileWork;
class FVoxelLandPreviewWork;
struct FEdGraphEditAction;
struct FPropertyChangedEvent;
struct FVoxelLandZoneProfile;
struct FVoxelLandSerializedProgram;
struct FGraphActionListBuilderBase;
struct FEdGraphSchemaAction;

class FVoxelLandGraphToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
    virtual ~FVoxelLandGraphToolkit() override;
    void Open(UVoxelLandTerrainGraph* InAsset, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host);
    static bool StoreCompiledProgram(UVoxelLandTerrainGraph& Graph, FVoxelLandSerializedProgram&& Payload);
    void Stop();
    virtual FName GetToolkitFName() const override { return TEXT("VoxelLandGraph"); }
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("VoxelLand"); }
    virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.2f, 0.6f, 0.3f); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FVoxelLandGraphToolkit"); }
private:
    TSharedRef<SDockTab> SpawnGraph(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDetails(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDiagnostics(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnPreview(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnPalette(const FSpawnTabArgs& Args);
    void CollectPalette(FGraphActionListBuilderBase& OutActions);
    void ActivatePalette(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions);
    void CreateMirror();
    bool CaptureMirror(FString& Error);
    void GraphChanged(const FEdGraphEditAction& Action);
    void PropertiesChanged(const FPropertyChangedEvent& Event);
    void SelectionChanged(const TSet<UObject*>& Selection);
    void RequestCompile();
    void BindCommands();
    void DeleteSelected();
    void CopySelected();
    void PasteNodes();
    void DuplicateSelected();
    FReply CompileClicked();
    FText StatusText() const;
    void StartPreview();
    bool BuildPreviewProfile(FVoxelLandZoneProfile& Profile, FString& Error) const;
    void CancelPreview();
    void ClearPreview();
    void SeedCommitted(const FText& Text, ETextCommit::Type CommitType);
    void MaterialChanged(const FAssetData& Material);
    void ApplyPreviewMaterial(UMaterialInterface* Material);
    void ApplyMaterialSetup();
    void SettingsChanged(const FAssetData& Settings);
    void PreviewPropertiesChanged(const FPropertyChangedEvent& Event);
    void BeginPIE(bool bSimulating);
    void EndPIE(bool bSimulating);
    FReply ResetPreviewCamera();
    FReply FocusError();
    FReply AddParameter(EVoxelLandGraphValueType Type);
    bool PollJobs(float DeltaTime);
    void ProcessCompileWork();
    void ProcessPreviewWork();
    bool PublishPreview(const FVoxelLandPreviewWork& Finished);
    TObjectPtr<UVoxelLandTerrainGraph> Asset = nullptr;
    TObjectPtr<UEdGraph> Graph = nullptr;
    TSharedPtr<SGraphEditor> GraphWidget;
    TSharedPtr<SGraphActionMenu> PaletteWidget;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<IDetailsView> PreviewDetails;
    TSharedPtr<FUICommandList> Commands;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<SVoxelLandPreviewViewport> PreviewViewport;
    TArray<TObjectPtr<UVoxelLandChunkComponent>> PreviewComponents;
    TObjectPtr<UMaterialInterface> PreviewMaterial = nullptr;
    TObjectPtr<UVoxelLandWorldSettings> PreviewSettings = nullptr;
    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> PreviewCancel;
    int64 PreviewSeed = 1234;
    int32 PreviewHorizontalRadius = 1;
    int32 PreviewVerticalRadius = 1;
    uint64 PreviewRevision = 0;
    FDelegateHandle GraphChangedHandle;
    FDelegateHandle BeginPIEHandle;
    FDelegateHandle EndPIEHandle;
    FTSTicker::FDelegateHandle PollHandle;
    TArray<TFuture<void>> WorkerTasks;
    TSharedPtr<FVoxelLandCompileWork, ESPMode::ThreadSafe> CompileWork;
    TSharedPtr<FVoxelLandPreviewWork, ESPMode::ThreadSafe> PreviewWork;
    double CompileDue = 0.0;
    bool bCompileRequested = false;
    bool bPreviewRequested = false;
    bool bStopped = false;
    bool bPIE = false;
    FString Status = TEXT("Stale: compile graph before use.");
    FString MaterialDiagnostic;
    uint64 Revision = 0;
    uint64 LastSourceHash = 0;
    uint64 PresentationRevision = 0;
    FGuid ErrorNode;
    double LastCompileMs = 0.0;
    bool bLoading = false;
};
