#include "VoxelLandPCGComponent.h"

#include "VoxelLandPCGCell.h"
#include "Async/Async.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Engine/World.h"
#include "Graph/Compilation/VoxelLandMaskGraphProgram.h"
#include "Graph/Compilation/VoxelLandMaterialSignalBindings.h"
#include "Graph/Compilation/VoxelLandTerrainSignalCapture.h"
#include "UObject/UObjectGlobals.h"
#include "PCGGraph.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/VoxelLandWorld.h"

UVoxelLandPCGComponent::UVoxelLandPCGComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bTickInEditor = true;
}

void UVoxelLandPCGComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!ResolveSourceWorld())
	{
		ActiveSourceWorld = Cast<AVoxelLandWorld>(GetOwner());
	}
	SetComponentTickEnabled(CanRun());
}

void UVoxelLandPCGComponent::OnUnregister()
{
	ClearPreview();
	Super::OnUnregister();
}

void UVoxelLandPCGComponent::SetSourceWorld(AVoxelLandWorld* InWorld)
{
	if (ResolveSourceWorld() == InWorld)
	{
		return;
	}
	RetireAllCells();
	ActiveSourceWorld = InWorld;
	Epoch = 0;
	bRefreshQueued = true;
}

void UVoxelLandPCGComponent::SetWorldConfig(UVoxelLandWorldConfig* InConfig)
{
	if (ResolveWorldConfig() != InConfig)
	{
		ActiveWorldConfig = InConfig;
		bRefreshQueued = true;
	}
}

AVoxelLandWorld* UVoxelLandPCGComponent::ResolveSourceWorld() const
{
	return ActiveSourceWorld.IsValid() ? ActiveSourceWorld.Get() : SourceWorld.Get();
}

UVoxelLandWorldConfig* UVoxelLandPCGComponent::ResolveWorldConfig() const
{
	return ActiveWorldConfig.IsValid() ? ActiveWorldConfig.Get() : WorldConfig.Get();
}

void UVoxelLandPCGComponent::RefreshPreview(const FBox& WorldBounds)
{
	if (!GetWorld() || !WorldBounds.IsValid)
	{
		return;
	}
	const FVector Offset(GetWorld()->OriginLocation);
	PreviewLogicalBounds = WorldBounds.ShiftBy(Offset);
	bPreview = true;
	RefreshPlacement();
}

void UVoxelLandPCGComponent::RefreshPlacement()
{
	bRefreshQueued = true;
	RefreshAt = 0.0;
	SetComponentTickEnabled(CanRun());
#if WITH_EDITOR
	BindAuthoredGraphs();
#endif
}

void UVoxelLandPCGComponent::ClearPreview()
{
#if WITH_EDITOR
	UnbindAuthoredGraphs();
#endif
	RetireAllCells();
	MaskCompile = {};
	MaskProgram.Reset();
	MaskSignals.Reset();
	MaskError.Reset();
	PlacementStatus.Reset();
	bPreview = false;
	bRefreshQueued = true;
	SetComponentTickEnabled(false);
}

void UVoxelLandPCGComponent::RetireAllCells()
{
	for (TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
	{
		CancelCell(Entry.Value);
		if (AVoxelLandPCGCell* Visible = Entry.Value.Visible.Get())
		{
			Visible->Destroy();
		}
	}
	Cells.Reset();
	CellOrder.Reset();
}

bool UVoxelLandPCGComponent::CanRun() const
{
	const UWorld* World = GetWorld();
	return World && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer && FApp::CanEverRender()
		&& ((World->IsGameWorld() && bGenerateInGame) || (World->WorldType == EWorldType::Editor && bPreview));
}

void UVoxelLandPCGComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_Scheduler);
	if (!CanRun())
	{
		ClearPreview();
		return;
	}

	double ChunkSize = 0.0;
	uint64 CurrentEpoch = 0;
	AVoxelLandWorld* Terrain = ResolveSourceWorld();
	if (!IsValid(Terrain) || Terrain->GetWorld() != GetWorld()
		|| !Terrain->GetTerrainPlacementGrid(ChunkSize, CurrentEpoch))
	{
		if (!Cells.IsEmpty())
		{
			RetireAllCells();
			Epoch = 0;
			bRefreshQueued = true;
		}
		PlacementStatus = TEXT("Waiting for voxel terrain");
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (CurrentEpoch != Epoch || (bRefreshQueued && Now >= RefreshAt))
	{
		if (Epoch != 0 && CurrentEpoch != Epoch)
		{
			RetireAllCells();
		}
		Epoch = CurrentEpoch;
		InvalidatePlacement();
		bRefreshQueued = false;
	}
	if (bRefreshQueued || !bLayersValid || !UpdateMask())
	{
		return;
	}
	if (Now >= NextDemandUpdate)
	{
		BuildDesiredCells(ChunkSize * FMath::Clamp(CellChunks, 1, 4));
		NextDemandUpdate = Now + 0.25;
	}
	ProcessCells();
}

void UVoxelLandPCGComponent::InvalidatePlacement()
{
	for (TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
	{
		CancelCell(Entry.Value);
		Entry.Value.bComplete = false;
		Entry.Value.Dependencies.Reset();
	}
	NextDemandUpdate = 0.0;
	bLayersValid = ValidateLayers();
	if (bLayersValid)
	{
		StartMaskCompile();
	}
}

void UVoxelLandPCGComponent::StartMaskCompile()
{
	MaskProgram.Reset();
	MaskSignals.Reset();
	MaskError.Reset();
	MaskCompile = {};
	const UVoxelLandWorldConfig* Config = ResolveWorldConfig();
	if (!Config || Config->MaskGraph.IsNull())
	{
		return;
	}
	const UVoxelLandTerrainGraph* Graph = Config->MaskGraph.LoadSynchronous();
	FVoxelLandGraphSnapshot Snapshot;
	if (!Graph || !Graph->CaptureSnapshot(Snapshot, MaskError))
	{
		if (MaskError.IsEmpty())
		{
			MaskError = TEXT("Mask Graph could not be loaded");
		}
		return;
	}
	FVoxelLandTerrainSignalCapture Capture;
	FString CaptureError;
	const bool bCaptured = FVoxelLandMaterialSignalBindings::CaptureTerrain(*Config, Capture, CaptureError);
	MaskCompile = Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Capture = MoveTemp(Capture),
														 CaptureError = MoveTemp(CaptureError), bCaptured]() {
		TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_CompileMask);
		FVoxelLandPCGMaskCompile Result;
		if (!FVoxelLandMaskGraphProgram::Compile(Snapshot, Result.Program, Result.Error))
		{
			if (Result.Error.IsEmpty())
			{
				Result.Error = TEXT("Mask Graph compilation failed");
			}
			return Result;
		}
		if (!Result.Program->GetTerrainSignals().IsEmpty())
		{
			if (!bCaptured)
			{
				Result.Error =
					CaptureError.IsEmpty() ? TEXT("Terrain signals are unavailable for the Mask Graph") : CaptureError;
			}
			else if (!FVoxelLandMaterialSignalBindings::BuildTerrain(
						 Capture, Result.Program->GetTerrainSignals(), Result.Signals, Result.Error)
					 && Result.Error.IsEmpty())
			{
				Result.Error = TEXT("Mask Graph terrain signal compilation failed");
			}
		}
		return Result;
	});
}

bool UVoxelLandPCGComponent::UpdateMask()
{
	if (MaskCompile.IsValid())
	{
		if (!MaskCompile.IsReady())
		{
			PlacementStatus = TEXT("Compiling Mask Graph");
			return false;
		}
		FVoxelLandPCGMaskCompile Result = MaskCompile.Get();
		MaskCompile = {};
		MaskProgram = MoveTemp(Result.Program);
		MaskSignals = MoveTemp(Result.Signals);
		MaskError = MoveTemp(Result.Error);
	}
	if (!MaskError.IsEmpty())
	{
		PlacementStatus = MaskError;
		return false;
	}
	return true;
}

int32 UVoxelLandPCGComponent::GetVisibleCellCount() const
{
	int32 Count = 0;
	for (const TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
	{
		Count += Entry.Value.Visible.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 UVoxelLandPCGComponent::GetVisibleInstanceCount() const
{
	int32 Count = 0;
	for (const TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
	{
		if (const AVoxelLandPCGCell* Cell = Entry.Value.Visible.Get())
		{
			Count += Cell->GetInstanceCount();
		}
	}
	return Count;
}

#if WITH_EDITOR
void UVoxelLandPCGComponent::BindAuthoredGraphs()
{
	UnbindAuthoredGraphs();
	if (!bPreview || !bLiveRefresh)
	{
		return;
	}
	AuthoringChangedHandle =
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UVoxelLandPCGComponent::OnAuthoringChanged);
	for (const FVoxelLandPCGLayer& Layer : Layers)
	{
		UPCGGraphInterface* Graph = Layer.Graph;
		if (!Graph || BoundGraphs.Contains(Graph))
		{
			continue;
		}
		BoundGraphs.Add(Graph);
		Graph->OnGraphChangedDelegate.AddWeakLambda(this, [this](UPCGGraphInterface*, EPCGChangeType) {
			if (bLiveRefresh)
			{
				bRefreshQueued = true;
				RefreshAt = FPlatformTime::Seconds() + 0.2;
			}
		});
	}
}

void UVoxelLandPCGComponent::UnbindAuthoredGraphs()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(AuthoringChangedHandle);
	AuthoringChangedHandle.Reset();
	for (const TWeakObjectPtr<UPCGGraphInterface>& WeakGraph : BoundGraphs)
	{
		if (UPCGGraphInterface* Graph = WeakGraph.Get())
		{
			Graph->OnGraphChangedDelegate.RemoveAll(this);
		}
	}
	BoundGraphs.Reset();
}

void UVoxelLandPCGComponent::OnAuthoringChanged(UObject* Object, FPropertyChangedEvent&)
{
	const UVoxelLandWorldConfig* Config = ResolveWorldConfig();
	if (Object && bLiveRefresh && (Object == this || Object == Config || (Config && Object == Config->MaskGraph.Get())))
	{
		bRefreshQueued = true;
		RefreshAt = FPlatformTime::Seconds() + 0.2;
	}
}
#endif
