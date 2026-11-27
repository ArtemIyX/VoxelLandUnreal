#include "Worlds/VoxelLandTerrainPreview.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "VoxelLandPCGComponent.h"
#include "World/VoxelLandWorld.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "VoxelLandTerrainPreview"

AVoxelLandTerrainPreview::AVoxelLandTerrainPreview()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Placement = CreateDefaultSubobject<UVoxelLandPCGComponent>(TEXT("Placement"));
	Placement->bGenerateInGame = false;
	Placement->bLiveRefresh = false;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PreviewStatus = LOCTEXT("Idle", "Preview is clear.");
}

bool AVoxelLandTerrainPreview::CanPreview() const
{
	const UWorld* World = GetWorld();
	return !IsTemplate() && !IsActorBeingDestroyed() && World && World->WorldType == EWorldType::Editor
		&& !World->bIsTearingDown;
}

bool AVoxelLandTerrainPreview::ShouldTickIfViewportsOnly() const
{
	return CanPreview();
}

void AVoxelLandTerrainPreview::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!CanPreview() || ObjectModifiedHandle.IsValid())
	{
		return;
	}
	ObjectModifiedHandle =
		FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &AVoxelLandTerrainPreview::HandleObjectModified);
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
		this, &AVoxelLandTerrainPreview::HandleObjectPropertyChanged);
	if (bLiveRefresh && WorldConfig)
	{
		QueueRefresh(true);
	}
}

void AVoxelLandTerrainPreview::UnbindAuthoringChanges()
{
	FCoreUObjectDelegates::OnObjectModified.Remove(ObjectModifiedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
	ObjectModifiedHandle.Reset();
	ObjectPropertyChangedHandle.Reset();
}

void AVoxelLandTerrainPreview::PostEditChangeProperty(FPropertyChangedEvent& Change)
{
	Super::PostEditChangeProperty(Change);
	const FName Property = Change.GetMemberPropertyName();
	if (Property == GET_MEMBER_NAME_CHECKED(AVoxelLandTerrainPreview, bLiveRefresh))
	{
		Placement->bLiveRefresh = bLiveRefresh;
		if (VisibleWorld)
		{
			if (bLiveRefresh)
			{
				RefreshPlacement();
			}
			return;
		}
	}
	if (Property == GET_MEMBER_NAME_CHECKED(AVoxelLandTerrainPreview, WorldConfig)
		|| Property == GET_MEMBER_NAME_CHECKED(AVoxelLandTerrainPreview, PreviewExtentCm)
		|| Property == GET_MEMBER_NAME_CHECKED(AVoxelLandTerrainPreview, bLiveRefresh))
	{
		QueueRefresh(true);
	}
}

void AVoxelLandTerrainPreview::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished)
	{
		QueueRefresh(true);
	}
}

void AVoxelLandTerrainPreview::PostEditUndo()
{
	Super::PostEditUndo();
	QueueRefresh(true);
}

void AVoxelLandTerrainPreview::HandleObjectModified(UObject* Object)
{
	if (!Object || !IsValid(WorldConfig))
	{
		return;
	}
	if (const UObject* Graph = WorldConfig->Graph.Get(); Graph && (Object == Graph || Object->IsIn(Graph)))
	{
		QueueRefresh(true);
		return;
	}
	if (const UObject* Graph = WorldConfig->MaskGraph.Get(); Graph && (Object == Graph || Object->IsIn(Graph)))
	{
		QueueRefresh(false);
	}
}

void AVoxelLandTerrainPreview::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Change)
{
	if (Object && Object == WorldConfig.Get())
	{
		QueueRefresh(Change.GetMemberPropertyName() != GET_MEMBER_NAME_CHECKED(UVoxelLandWorldConfig, MaskGraph));
		return;
	}
	HandleObjectModified(Object);
}

void AVoxelLandTerrainPreview::QueueRefresh(bool bTerrain)
{
	if (!CanPreview())
	{
		return;
	}
	if (!bLiveRefresh)
	{
		if (VisibleWorld && bTerrain)
		{
			SetStatus(LOCTEXT("Stale", "Preview settings changed. Generate Preview to update terrain."));
		}
		return;
	}
	bQueuedTerrainRefresh |= bTerrain;
	bQueuedPlacementRefresh |= !bTerrain;
	RefreshTime = FPlatformTime::Seconds() + RefreshDebounceSeconds;
	SetActorTickEnabled(true);
}

void AVoxelLandTerrainPreview::GeneratePreview()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_EditorPreview_Request);
	if (!CanPreview())
	{
		return;
	}
	bQueuedTerrainRefresh = false;
	bQueuedPlacementRefresh = false;
	bPendingDemand = false;
	RetireWorld(PendingWorld);
	PendingConfig = nullptr;
	RequestedBounds = FBox(ForceInit);

	if (!IsValid(WorldConfig) || WorldConfig->IsInfinite())
	{
		FailCandidate(LOCTEXT("InvalidConfig", "Assign a finite VoxelLand World Config."));
		return;
	}
	if (PreviewExtentCm.ContainsNaN() || PreviewExtentCm.GetMin() <= 0.0)
	{
		FailCandidate(LOCTEXT("InvalidExtent", "Preview extent must be positive and finite."));
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.ObjectFlags = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bHideFromSceneOutliner = true;
	Params.bTemporaryEditorActor = true;
	PendingWorld =
		GetWorld()->SpawnActor<AVoxelLandWorld>(AVoxelLandWorld::StaticClass(), FTransform::Identity, Params);
	if (!IsValid(PendingWorld))
	{
		FailCandidate(LOCTEXT("SpawnFailed", "Unable to create the session's terrain preview."));
		return;
	}

	PendingConfig = WorldConfig;
	RequestedBounds = FBox::BuildAABB(GetActorLocation(), PreviewExtentCm);
	AVoxelLandWorld* Candidate = PendingWorld.Get();
	const bool bStarted = Candidate->InitializeFiniteEditorPreview(PendingConfig);
	if (!CanPreview() || PendingWorld != Candidate || !IsValid(Candidate))
	{
		return;
	}
	if (!bStarted)
	{
		const FText Error = FText::FromString(Candidate->LastInitializationError);
		FailCandidate(Error);
		return;
	}
	SetStatus(LOCTEXT("Initializing", "Preparing terrain. The previous preview stays visible."));
	SetActorTickEnabled(true);
}

void AVoxelLandTerrainPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!CanPreview())
	{
		return;
	}
	if ((bQueuedTerrainRefresh || bQueuedPlacementRefresh) && FPlatformTime::Seconds() >= RefreshTime)
	{
		const bool bRegenerateTerrain = bQueuedTerrainRefresh;
		bQueuedTerrainRefresh = false;
		bQueuedPlacementRefresh = false;
		if (bLiveRefresh)
		{
			if (bRegenerateTerrain)
			{
				GeneratePreview();
			}
			else
			{
				RefreshPlacement();
			}
		}
	}
	AdvanceCandidate();
	SetActorTickEnabled(IsValid(PendingWorld) || bQueuedTerrainRefresh || bQueuedPlacementRefresh);
}

void AVoxelLandTerrainPreview::AdvanceCandidate()
{
	if (!IsValid(PendingWorld) || PendingWorld->bInitializing)
	{
		return;
	}
	if (!bPendingDemand)
	{
		FString Error = PendingWorld->LastInitializationError;
		if (!Error.IsEmpty() || !PendingWorld->RequestEditorPreviewBounds(RequestedBounds, PendingBounds, Error))
		{
			FailCandidate(FText::FromString(Error));
			return;
		}
		bPendingDemand = true;
		SetStatus(LOCTEXT("Rendering", "Generating terrain for the preview area."));
	}

	FString Error;
	if (!PendingWorld->PollEditorTerrainPreview(Error))
	{
		return;
	}
	if (!Error.IsEmpty())
	{
		FailCandidate(FText::FromString(Error));
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_EditorPreview_Publish);
	AVoxelLandWorld* OldWorld = VisibleWorld.Get();
	if (IsValid(OldWorld))
	{
		OldWorld->SetEditorTerrainPreviewVisible(false);
	}
	VisibleWorld = PendingWorld;
	VisibleConfig = PendingConfig;
	VisibleBounds = PendingBounds;
	PendingWorld = OldWorld;
	PendingConfig = nullptr;
	bPendingDemand = false;
	// An old forest belongs to the old surface. Mask-only refreshes keep their existing placement.
	Placement->ClearPreview();
	VisibleWorld->SetEditorTerrainPreviewVisible(true);
	RefreshPlacement();
	RetireWorld(PendingWorld);
	SetStatus(VisibleBounds.IsValid ? LOCTEXT("Ready", "Terrain is ready. See Placement for PCG progress.")
									: LOCTEXT("Outside", "The preview area contains no supported terrain chunks."));
}

void AVoxelLandTerrainPreview::RefreshPlacement()
{
	if (!CanPreview() || !IsValid(VisibleWorld))
	{
		return;
	}
	Placement->bLiveRefresh = bLiveRefresh;
	Placement->SetWorldConfig(VisibleConfig.Get());
	Placement->SetSourceWorld(VisibleWorld.Get());
	if (VisibleBounds.IsValid)
	{
		Placement->RefreshPreview(VisibleBounds);
	}
	else
	{
		Placement->ClearPreview();
	}
}

void AVoxelLandTerrainPreview::FailCandidate(const FText& Reason)
{
	RetireWorld(PendingWorld);
	PendingConfig = nullptr;
	bPendingDemand = false;
	SetStatus(FText::Format(LOCTEXT("Failed", "Terrain preview failed. {0}"), Reason));
}

void AVoxelLandTerrainPreview::RetireWorld(TObjectPtr<AVoxelLandWorld>& World)
{
	AVoxelLandWorld* Actor = World.Get();
	World = nullptr;
	if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
	{
		Actor->Destroy();
	}
}

void AVoxelLandTerrainPreview::ReleasePreview()
{
	bQueuedTerrainRefresh = false;
	bQueuedPlacementRefresh = false;
	bPendingDemand = false;
	SetActorTickEnabled(false);
	if (IsValid(Placement))
	{
		Placement->ClearPreview();
		Placement->SetSourceWorld(nullptr);
		Placement->SetWorldConfig(nullptr);
	}
	RetireWorld(PendingWorld);
	RetireWorld(VisibleWorld);
	PendingConfig = nullptr;
	VisibleConfig = nullptr;
	RequestedBounds = FBox(ForceInit);
	PendingBounds = FBox(ForceInit);
	VisibleBounds = FBox(ForceInit);
}

void AVoxelLandTerrainPreview::ClearPreview()
{
	ReleasePreview();
	SetStatus(LOCTEXT("Idle", "Preview is clear."));
}

void AVoxelLandTerrainPreview::Destroyed()
{
	UnbindAuthoringChanges();
	ReleasePreview();
	Super::Destroyed();
}

void AVoxelLandTerrainPreview::BeginDestroy()
{
	UnbindAuthoringChanges();
	Super::BeginDestroy();
}

void AVoxelLandTerrainPreview::SetStatus(const FText& Text)
{
	PreviewStatus = Text;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

#undef LOCTEXT_NAMESPACE
