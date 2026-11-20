#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "UObject/GCObject.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "Data/Structs/VoxelLandGraphDiagnostic.h"
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
class FVoxelLandPreviewFence;
class FPackageReloadedEvent;
enum class EPackageReloadPhase : uint8;
struct FEdGraphEditAction;
struct FPropertyChangedEvent;
struct FVoxelLandReloadTestBarrier;
struct FVoxelLandZoneProfile;
struct FVoxelLandSerializedProgram;
struct FGraphActionListBuilderBase;
struct FEdGraphSchemaAction;

class FVoxelLandGraphToolkit : public FAssetEditorToolkit, public FGCObject, public FEditorUndoClient
{
public:
    virtual ~FVoxelLandGraphToolkit() override;
    void Open(UVoxelLandTerrainGraph* InAsset, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host);
    static bool StoreCompiledProgram(UVoxelLandTerrainGraph& Graph, FVoxelLandSerializedProgram&& Payload);
    void Stop();
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;
#if WITH_DEV_AUTOMATION_TESTS
    void InitializeForTests(UVoxelLandTerrainGraph* InAsset);
    uint64 GetRevisionForTests() const { return Revision; }
    int32 GetPanelRefreshCountForTests() const { return PanelRefreshCountForTests; }
    void CreatePanelForTests();
    UVoxelLandWorldSettings* GetPreviewSettingsForTests() const { return PreviewSettings; }
    uint64 GetPreviewRevisionForTests() const { return PreviewRevision; }
    int32 GetMaterialApplyCountForTests() const { return MaterialApplyCountForTests; }
    int32 GetPreviewStartCountForTests() const { return PreviewStartCountForTests; }
    void SynchronizePreviewForTests() { SynchronizePreviewSettings(true); }
    void SetPreviewRadiusForTests(int32 Value, bool bHorizontal) { CommitPreviewRadius(Value, bHorizontal); }
    void SetPreviewSeedForTests(int64 Value) { SeedCommitted(FText::AsCultureInvariant(LexToString(Value)), ETextCommit::OnEnter); }
    int32 GetPreviewHorizontalRadiusForTests() const { return PreviewHorizontalRadius; }
    void SetMaterialPickerForTests(UMaterialInterface* Material);
    void SetSettingsPickerForTests(UVoxelLandWorldSettings* Settings);
    FString GetMaterialPickerPathForTests() const { return MaterialPickerPath(); }
    UMaterialInterface* GetEffectiveMaterialForTests() const { return PreviewMaterial; }
    FString GetSettingsPickerPathForTests() const { return SettingsPickerPath(); }
    void SetDiagnosticForTests(const FVoxelLandGraphDiagnostic& InDiagnostic) { ErrorDiagnostic = InDiagnostic; }
    const FVoxelLandGraphDiagnostic& GetDiagnosticForTests() const { return ErrorDiagnostic; }
    bool ResolveDiagnosticForTests(UEdGraphNode*& Node, UEdGraphPin*& Pin) const { return ResolveDiagnosticTarget(Node, Pin); }
    void OnReplacedForTests(const TMap<UObject*, UObject*>& Map) { ObjectsReplaced(Map); }
    void PackageReloadForTests(EPackageReloadPhase Phase, FPackageReloadedEvent* Event) { PackageReloaded(Phase, Event); }
    void SetReloadDrainingForTests(TFunction<void()> Callback) { ReloadDrainingForTests = MoveTemp(Callback); }
    void SetPreviewReloadBarrierForTests(TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier) { PreviewReloadBarrierForTests = MoveTemp(Barrier); }
    void InitializePreviewForTests();
    FBox GetPreviewBoundsForTests() const;
    FBox GetComponentBoundsForTests() const;
    bool FramePreviewForTests();
    void PollJobsForTests() { PollJobs(0.0f); }
    const FString& GetStatusForTests() const { return Status; }
    bool HasPreviewFenceForTests() const { return PreviewFence.IsValid(); }
    void StartPreviewForTests() { StartPreview(); }
    void RequestCompileForTests() { RequestCompile(); }
    bool HasCompileWorkForTests() const { return CompileWork.IsValid(); }
    void AddWorkerForTests(TFuture<void>&& Task) { WorkerTasks.Add(MoveTemp(Task)); }
    bool IsStoppedForTests() const { return bStopped; }
    bool IsReloadPausedForTests() const { return bReloadPaused; }
#endif
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
    bool CaptureMirror(FString& Error, FVoxelLandGraphDiagnostic* Diagnostic = nullptr);
    void GraphChanged(const FEdGraphEditAction& Action);
    void SynchronizeGraph();
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
    void CommitPreviewRadius(int32 Value, bool bHorizontal);
    void MaterialSetupPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
    void PackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
    void ObjectsReplaced(const TMap<UObject*, UObject*>& Replacements);
    void ObjectsReinstanced(const TMap<UObject*, UObject*>& Replacements);
    void PauseForReload(bool bGraphAffected);
    void ResumeAfterReload();
    void MaterialChanged(const FAssetData& Material);
    FString MaterialPickerPath() const;
    FString SettingsPickerPath() const;
    void ApplyPreviewMaterial(UMaterialInterface* Material);
    void ApplyMaterialSetup();
    void SettingsChanged(const FAssetData& Settings);
    void PreviewPropertiesChanged(const FPropertyChangedEvent& Event);
    void SynchronizePreviewSettings(bool bApplyChanges);
    void BeginPIE(bool bSimulating);
    void EndPIE(bool bSimulating);
    FReply ResetPreviewCamera();
    FReply FramePreviewTerrain();
    FReply FocusError();
    bool ResolveDiagnosticTarget(UEdGraphNode*& OutNode, UEdGraphPin*& OutPin) const;
    FReply AddParameter(EVoxelLandGraphValueType Type);
    bool PollJobs(float DeltaTime);
    void ProcessCompileWork();
    void ProcessPreviewWork();
    void PollPreviewFence();
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
    TSoftObjectPtr<UMaterialInterface> SelectedPreviewMaterial;
    TSoftObjectPtr<UVoxelLandWorldSettings> SelectedPreviewSettings;
    TObjectPtr<UVoxelLandWorldSettings> PreviewSettings = nullptr;
    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> PreviewCancel;
    int64 PreviewSeed = 1234;
    int32 PreviewHorizontalRadius = 1;
    int32 PreviewVerticalRadius = 1;
    uint64 PreviewRevision = 0;
    FDelegateHandle GraphChangedHandle;
    FDelegateHandle BeginPIEHandle;
    FDelegateHandle EndPIEHandle;
    FDelegateHandle MaterialSetupChangedHandle;
    FDelegateHandle PackageReloadHandle;
    FDelegateHandle ObjectsReplacedHandle;
    FDelegateHandle ObjectsReinstancedHandle;
    FTSTicker::FDelegateHandle PollHandle;
    TArray<TFuture<void>> WorkerTasks;
    TSharedPtr<FVoxelLandCompileWork, ESPMode::ThreadSafe> CompileWork;
    TSharedPtr<FVoxelLandPreviewWork, ESPMode::ThreadSafe> PreviewWork;
    TSharedPtr<FVoxelLandPreviewFence> PreviewFence;
    double SemanticChangeTime = 0.0;
    double CompileDue = 0.0;
    bool bCompileRequested = false;
    bool bPreviewRequested = false;
    bool bStopped = false;
    bool bReloadPaused = false;
    bool bPackageReloadInProgress = false;
    bool bReloadGraphAffected = false;
    bool bResumePreviewAfterReload = false;
    FString StatusBeforeReload;
    bool bHasEditorWindow = false;
    bool bRegisteredForUndo = false;
    bool bPIE = false;
    FString Status = TEXT("Stale: compile graph before use.");
    FString MaterialDiagnostic;
    TArray<uint8> PreviewDensitySnapshot;
    TArray<uint8> PreviewMaterialSnapshot;
    bool bHasPreviewSettingsSnapshot = false;
    uint64 Revision = 0;
    uint64 LastSourceHash = 0;
    uint64 PresentationRevision = 0;
    FVoxelLandGraphDiagnostic ErrorDiagnostic;
    double LastCompileMs = 0.0;
    bool bLoading = false;
#if WITH_DEV_AUTOMATION_TESTS
    int32 PanelRefreshCountForTests = 0;
    int32 MaterialApplyCountForTests = 0;
    int32 PreviewStartCountForTests = 0;
    TFunction<void()> ReloadDrainingForTests;
    TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> PreviewReloadBarrierForTests;
#endif
};
