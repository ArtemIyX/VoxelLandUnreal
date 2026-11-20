#include "World/VoxelLandWorld.h"

#include "Async/Async.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Structs/VoxelLandWorldState.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandProgramCodec.h"
#if WITH_EDITOR
#include "UObject/PackageReload.h"
#endif
#if !UE_SERVER
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#endif

namespace
{
void CopyOverrides(const UVoxelLandWorldSettings& Settings, const FVoxelLandSerializedProgram& Payload,
    FVoxelLandZoneProfile& Profile)
{
    Profile.VoxelSizeCm = Settings.VoxelSizeCm;
    for (const FVoxelLandParameterOverride& Override : Settings.Overrides)
    {
        const FVoxelLandSerializedParameter* Parameter = Payload.Parameters.FindByPredicate(
            [&Override](const FVoxelLandSerializedParameter& Candidate) { return Candidate.Id == Override.ParameterId; });
        // Unreachable declarations need no generator slot.
        if (!Parameter)
        {
            continue;
        }
        switch (Override.Type)
        {
            case EVoxelLandGraphValueType::Float:
                Profile.FloatOverrides.Add(Parameter->Name, Override.FloatValue);
                break;
            case EVoxelLandGraphValueType::Int32:
                Profile.IntOverrides.Add(Parameter->Name, Override.IntValue);
                break;
            case EVoxelLandGraphValueType::Bool:
                Profile.BoolOverrides.Add(Parameter->Name, Override.BoolValue);
                break;
        }
    }
}

bool OverridesEqual(const TArray<FVoxelLandParameterOverride>& A, const TArray<FVoxelLandParameterOverride>& B)
{
    if (A.Num() != B.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
        if (A[Index].ParameterId != B[Index].ParameterId || A[Index].Type != B[Index].Type ||
            A[Index].FloatValue != B[Index].FloatValue || A[Index].IntValue != B[Index].IntValue ||
            A[Index].BoolValue != B[Index].BoolValue)
        {
            return false;
        }
    }
    return true;
}

class FSettingsInitializationRequest
{
public:
    FSettingsInitializationRequest(UVoxelLandWorldSettings& Settings, FVoxelLandGraphSnapshot&& InSnapshot,
        const FVoxelLandSerializedProgram& InPayload, FVoxelLandZoneProfile&& InProfile)
        : WeakSettings(&Settings), WeakGraph(Settings.Graph), Snapshot(MoveTemp(InSnapshot)), Payload(InPayload), Profile(MoveTemp(InProfile)),
          Overrides(Settings.Overrides), Seed(uint64(Settings.Seed)),
          SourceIdentity(FVoxelLandProgramCodec::SourceFingerprint(Snapshot)),
          HorizontalRadius(Settings.HorizontalRadius), VerticalRadius(Settings.VerticalRadius)
    {
    }

    bool IsCurrent() const
    {
        UVoxelLandWorldSettings* Current = WeakSettings.Get();
        FVoxelLandGraphSnapshot CurrentSnapshot;
        FString Error;
        return Current && Current->ValidateSettings(Error) && uint64(Current->Seed) == Seed &&
            Current->VoxelSizeCm == Profile.VoxelSizeCm && Current->HorizontalRadius == HorizontalRadius &&
            Current->VerticalRadius == VerticalRadius && OverridesEqual(Current->Overrides, Overrides) &&
            Current->Graph->CaptureSnapshot(CurrentSnapshot, Error) &&
            FVoxelLandProgramCodec::SourceFingerprint(CurrentSnapshot) == SourceIdentity &&
            Current->Graph->CompiledProgram.ProgramFingerprint == Payload.ProgramFingerprint &&
            Current->Graph->CompiledProgram.SourceFingerprint == Payload.SourceFingerprint;
    }

#if WITH_EDITOR
    bool References(const UObject* Object) const { return WeakSettings.Get() == Object || WeakGraph.Get() == Object; }
    bool ReferencesPackage(const UPackage* Package) const
    {
        const UObject* Settings = WeakSettings.Get();
        const UObject* Graph = WeakGraph.Get();
        return (Settings && Settings->GetOutermost() == Package) || (Graph && Graph->GetOutermost() == Package);
    }
#endif
    void Run(const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
        TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError) const
    {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        if (Cancel->Load() || !FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, OutError) || Cancel->Load())
        {
            return;
        }
        FVoxelLandGeneratorSettings Settings;
        Settings.Program = Program;
        Settings.WorldSeed = Seed;
        Settings.Profile = Profile;
        if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, OutGenerator))
        {
            OutError = Failure.Message;
        }
    }

private:
    TWeakObjectPtr<UVoxelLandWorldSettings> WeakSettings;
    TWeakObjectPtr<UVoxelLandTerrainGraph> WeakGraph;
    FVoxelLandGraphSnapshot Snapshot;
    FVoxelLandSerializedProgram Payload;
    FVoxelLandZoneProfile Profile;
    TArray<FVoxelLandParameterOverride> Overrides;
    uint64 Seed;
    uint64 SourceIdentity;
    int32 HorizontalRadius;
    int32 VerticalRadius;
};

#if !UE_SERVER
UMaterialInterface* ResolveTerrainMaterial(const UVoxelLandWorldSettings& Settings)
{
    const UVoxelLandMaterialSetup* Setup = Settings.MaterialSetup.LoadSynchronous();
    UMaterialInterface* Material = Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr;
    if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelLand: missing or unsupported terrain material; using the default surface material"));
        return UMaterial::GetDefaultMaterial(MD_Surface);
    }
    return Material;
}
#endif
}

class FVoxelLandInitializationJob
{
public:
    explicit FVoxelLandInitializationJob(const TSharedRef<FSettingsInitializationRequest, ESPMode::ThreadSafe>& InRequest)
        : Request(InRequest), Cancel(MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false))
    {
    }
    TSharedRef<FSettingsInitializationRequest, ESPMode::ThreadSafe> Request;
    TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Cancel;
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
    FString Error;
};

bool AVoxelLandWorld::InitializeFromSettings(UVoxelLandWorldSettings* Settings)
{
    check(IsInGameThread());
    UWorld* World = GetWorld();
    if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !World || World->bIsTearingDown || !HasActorBegunPlay())
    {
        LastInitializationError = TEXT("World actor is not ready for explicit initialization");
        return false;
    }
    Shutdown();
    LastInitializationError.Reset();
    if (!Settings || !Settings->ValidateSettings(LastInitializationError))
    {
        if (LastInitializationError.IsEmpty())
        {
            LastInitializationError = TEXT("World settings are missing");
        }
        return false;
    }
    FVoxelLandGraphSnapshot Snapshot;
    if (!Settings->Graph->CaptureSnapshot(Snapshot, LastInitializationError))
    {
        return false;
    }
    const FVoxelLandSerializedProgram Payload = Settings->Graph->CompiledProgram;
    FVoxelLandZoneProfile Profile;
    CopyOverrides(*Settings, Payload, Profile);
    const TSharedRef<FSettingsInitializationRequest, ESPMode::ThreadSafe> Request =
        MakeShared<FSettingsInitializationRequest, ESPMode::ThreadSafe>(*Settings, MoveTemp(Snapshot), Payload, MoveTemp(Profile));
    HorizontalRadius = Settings->HorizontalRadius;
    VerticalRadius = Settings->VerticalRadius;
#if !UE_SERVER
    if (!bDataOnly && GetNetMode() != NM_DedicatedServer)
    {
        SetTerrainMaterial(ResolveTerrainMaterial(*Settings));
    }
#endif
    const TSharedRef<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job =
        MakeShared<FVoxelLandInitializationJob, ESPMode::ThreadSafe>(Request);
    PendingInitialization = Job;
#if WITH_EDITOR
    InitPackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this, &AVoxelLandWorld::HandleInitPackageReload);
    InitObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &AVoxelLandWorld::HandleInitObjectsReplaced);
#endif
    bInitializing = true;
    SetActorTickEnabled(true);
    InitializationTask = Async(EAsyncExecution::ThreadPool, [Job]()
    {
        Job->Request->Run(Job->Cancel, Job->Generator, Job->Error);
    });
    return true;
}

void AVoxelLandWorld::StopPendingInitialization()
{
#if WITH_EDITOR
    UnbindInitReloadHandlers();
#endif
    if (PendingInitialization)
    {
        PendingInitialization->Cancel->Store(true);
    }
    if (InitializationTask.IsValid())
    {
        InitializationTask.Wait();
        InitializationTask = TFuture<void>();
    }
    PendingInitialization.Reset();
    bInitializing = false;
    if (!State)
    {
        SetActorTickEnabled(false);
    }
}

void AVoxelLandWorld::PollInitialization()
{
    if (!PendingInitialization || !InitializationTask.IsValid() || !InitializationTask.IsReady())
    {
        return;
    }
    InitializationTask.Wait();
    InitializationTask = TFuture<void>();
#if WITH_EDITOR
    UnbindInitReloadHandlers();
#endif
    TSharedPtr<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job = MoveTemp(PendingInitialization);
    bInitializing = false;
    if (!Job->Request->IsCurrent())
    {
        LastInitializationError = TEXT("Initialization result was superseded by changed settings or graph");
        SetActorTickEnabled(false);
        OnInitializationFinished.Broadcast(false);
        return;
    }
    if (!Job->Generator)
    {
        LastInitializationError = Job->Error.IsEmpty() ? TEXT("Generator initialization failed") : Job->Error;
        SetActorTickEnabled(false);
        OnInitializationFinished.Broadcast(false);
        return;
    }
    Initialize(Job->Generator);
    if (!State || State->Threads.IsEmpty())
    {
        Shutdown();
        LastInitializationError = TEXT("Unable to start terrain workers");
        OnInitializationFinished.Broadcast(false);
        return;
    }
    LastInitializationError.Reset();
    OnInitializationFinished.Broadcast(true);
}

void AVoxelLandWorld::CancelInitialization()
{
    check(IsInGameThread());
    if (!PendingInitialization)
    {
        return;
    }
    StopPendingInitialization();
    ++ConfigurationEpoch;
    LastInitializationError = TEXT("Initialization cancelled");
    OnInitializationFinished.Broadcast(false);
}

#if WITH_EDITOR
void AVoxelLandWorld::UnbindInitReloadHandlers()
{
    FCoreUObjectDelegates::OnPackageReloaded.Remove(InitPackageReloadHandle);
    FCoreUObjectDelegates::OnObjectsReplaced.Remove(InitObjectsReplacedHandle);
    InitPackageReloadHandle.Reset(); InitObjectsReplacedHandle.Reset();
}
void AVoxelLandWorld::HandleInitPackageReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
    if (Phase == EPackageReloadPhase::PrePackageLoad && Event && PendingInitialization &&
        PendingInitialization->Request->ReferencesPackage(Event->GetOldPackage()))
    {
        CancelInitialization();
    }
}
void AVoxelLandWorld::HandleInitObjectsReplaced(const TMap<UObject*, UObject*>& Map)
{
    if (!PendingInitialization) { return; }
    for (const TPair<UObject*, UObject*>& Replacement : Map)
    {
        if (PendingInitialization->Request->References(Replacement.Key))
        {
            CancelInitialization();
            return;
        }
    }
}
#endif
