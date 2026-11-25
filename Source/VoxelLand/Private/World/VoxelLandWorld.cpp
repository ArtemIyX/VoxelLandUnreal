#include "World/VoxelLandWorld.h"
#include "World/VoxelLandWorldCoordinates.h"
#if !UE_SERVER
#include "Rendering/VoxelLandChunkComponent.h"
#endif
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandMeshing.h"
#include "Data/Structs/VoxelLandPublication.h"
#include "Data/Structs/VoxelLandWorldState.h"
#include "Data/Structs/VoxelLandTestBarrier.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Async/Async.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "UObject/StrongObjectPtr.h"

TRACE_DECLARE_INT_COUNTER(VoxelQueued, TEXT("VoxelLand/Queued"));
TRACE_DECLARE_INT_COUNTER(VoxelOutstanding, TEXT("VoxelLand/Outstanding"));
TRACE_DECLARE_INT_COUNTER(VoxelReady, TEXT("VoxelLand/DataReady"));
TRACE_DECLARE_INT_COUNTER(VoxelPrimitives, TEXT("VoxelLand/UnhiddenResidentChunks"));
TRACE_DECLARE_INT_COUNTER(VoxelRegions, TEXT("VoxelLand/Regions"));
TRACE_DECLARE_INT_COUNTER(VoxelVertices, TEXT("VoxelLand/Vertices"));
TRACE_DECLARE_INT_COUNTER(VoxelTriangles, TEXT("VoxelLand/Triangles"));
TRACE_DECLARE_INT_COUNTER(VoxelStale, TEXT("VoxelLand/Stale"));
TRACE_DECLARE_INT_COUNTER(VoxelCancelled, TEXT("VoxelLand/Cancelled"));
TRACE_DECLARE_MEMORY_COUNTER(VoxelFieldBytes, TEXT("VoxelLand/FieldBytes"));
TRACE_DECLARE_MEMORY_COUNTER(VoxelMeshBytes, TEXT("VoxelLand/MeshBytes"));

namespace VoxelLandWorldPrivate
{
class FWorker final : public FRunnable
{
public:
	static constexpr int32 MaxWorkers = 4;
	static constexpr int32 MaxRecords = 8192;

	explicit FWorker(TSharedRef<FSharedWork, ESPMode::ThreadSafe> InWork)
		: Work(InWork) {}

	virtual uint32 Run() override
	{
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Current;
		TUniquePtr<FVoxelLandWorkerContext> Context;
		FVoxelLandMeshingScratch Scratch;
		while (!Work->bStop.Load())
		{
			FJob Job;
			{
				FScopeLock Lock(&Work->Mutex);
				if (!Work->Jobs.Dequeue(Job))
				{
					Job.Generator.Reset();
				}
			}
			if (!Job.Generator)
			{
				Work->Wake->Wait(100);
				continue;
			}
			FCompletion Done;
			Done.Job = Job;
			if (Job.Cancel->bCancelled.Load())
			{
				Done.bCancelled = true;
			}
			else
			{
				if (Current != Job.Generator || !Context)
				{
					Context.Reset();
					Current = Job.Generator;
					Done.Error = Current->CreateWorker(Context);
				}
				if (!Done.Error && !Context)
				{
					Done.Error.Code = EVoxelLandErrorCode::InvalidContext;
				}
				const EVoxelLandChunkClassification Proof = !Done.Error && Job.bRender
					                                            ? Current->ClassifyChunkBeforeSampling(Job.Key)
					                                            : EVoxelLandChunkClassification::NeedsSamples;
				if (!Done.Error)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_Generate);
					Done.Error = Current->GenerateChunkCancellable(Job.Key, *Context, Done.Data,
						[&] { return Job.Cancel->bCancelled.Load() || Work->bStop.Load(); });
				}
				Done.bCancelled = Job.Cancel->bCancelled.Load() || Work->bStop.Load();
				if (!Done.Error && !Done.bCancelled && Job.bRender && Proof ==
				    EVoxelLandChunkClassification::NeedsSamples
				    && VoxelLandMeshing::Classify(*Done.Data) == EVoxelLandChunkClassification::Surface)
				{
					auto Mesh = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
					const auto Result = VoxelLandMeshing::BuildSurfaceNets(
						*Done.Data, Current->GetProfile().VoxelSizeCm, Scratch, *Mesh,
						[&] { return Job.Cancel->bCancelled.Load() || Work->bStop.Load(); }, Current->IsM4Unlimited());
					if (Result == EVoxelLandMeshResult::Cancelled)
					{
						Done.bCancelled = true;
					}
					else if (Result == EVoxelLandMeshResult::InvalidInput)
					{
						Done.Error.Code = EVoxelLandErrorCode::InvalidParameter;
						Done.Error.Message = TEXT("Invalid mesh input");
					}
					else if (!Mesh->Indices.IsEmpty())
					{
						Done.Mesh = Mesh;
					}
				}
			}
#if WITH_DEV_AUTOMATION_TESTS
			if (Job.Barrier)
			{
				if (Job.Barrier->bMarkHash && Done.Data)
				{
					auto Marked = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>(*Done.Data);
					Marked->Hash = Job.Barrier->MarkerHash;
					Done.Data = Marked;
				}
				Job.Barrier->Reached->Trigger();
				while (!Work->bStop.Load() && !Job.Barrier->Release->Wait(5)) {}
			}
#endif
			Work->Completed.Enqueue(MoveTemp(Done));
		}
		// Context and its native nodes are destroyed on this same thread.
		Context.Reset();
		Current.Reset();
		return 0;
	}

private:
	TSharedRef<FSharedWork, ESPMode::ThreadSafe> Work;
};

}

using namespace VoxelLandWorldPrivate;

FVoxelLandWorldState::FVoxelLandWorldState() = default;
FVoxelLandWorldState::~FVoxelLandWorldState() = default;

AVoxelLandRegion::AVoxelLandRegion()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = false;
}
#if !UE_SERVER
bool AVoxelLandRegion::Publish(const FIntVector& Key, const FVector& Origin,
	TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh, UMaterialInterface* Material)
{
	check(IsInGameThread());
	FVector LocalOrigin;
	if (!GetWorld() || !VoxelLandWorldCoordinates::ToLocal(Origin, GetWorld()->OriginLocation, LocalOrigin))
	{
		return false;
	}
	TObjectPtr<UPrimitiveComponent>& Slot = Components.FindOrAdd(Key);
	UVoxelLandChunkComponent* Component = Cast<UVoxelLandChunkComponent>(Slot.Get());
	if (Slot && !Component)
	{
		return false;
	}
	if (!Component)
	{
		Component = NewObject<UVoxelLandChunkComponent>(this);
		Slot = Component;
		AddInstanceComponent(Component);
		Component->SetupAttachment(Root);
		Component->RegisterComponent();
	}
	// Unreal moves registered actors/components on an origin shift. New meshes
	// use the current origin so their positions agree with the shifted ones.
	Component->SetWorldLocation(LocalOrigin);
	Component->SetMaterial(0, Material);
	if (!Component->SetMesh(Mesh))
	{
		Remove(Key);
		return false;
	}
	Component->SetVisibility(true);
	return true;
}

void AVoxelLandRegion::Remove(const FIntVector& Key)
{
	if (TObjectPtr<UPrimitiveComponent>* Found = Components.Find(Key))
	{
		if (UVoxelLandChunkComponent* Component = Cast<UVoxelLandChunkComponent>(Found->Get()))
		{
			Component->SetVisibility(false);
			Component->ClearMesh();
			Component->DestroyComponent();
		}
		Components.Remove(Key);
	}
}

bool AVoxelLandRegion::IsChunkEffectivelyVisible(const FIntVector& Key) const
{
	const TObjectPtr<UPrimitiveComponent>* Component = Components.Find(Key);
	return !IsHidden() && Component && *Component && (*Component)->IsRegistered() && (*Component)->IsVisible();
}

void AVoxelLandRegion::SetTerrainMaterial(UMaterialInterface* Material)
{
	for (auto& Pair : Components)
	{
		if (Pair.Value)
		{
			Pair.Value->SetMaterial(0, Material);
		}
	}
}
#else
bool AVoxelLandRegion::Publish(const FIntVector&, const FVector&,
	TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>, UMaterialInterface*)
{
	return false;
}
void AVoxelLandRegion::Remove(const FIntVector&) {}
bool AVoxelLandRegion::IsChunkEffectivelyVisible(const FIntVector&) const { return false; }
void AVoxelLandRegion::SetTerrainMaterial(UMaterialInterface*) {}
#endif

AVoxelLandWorld::AVoxelLandWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorEnableCollision(false);
}

AVoxelLandWorld::AVoxelLandWorld(FVTableHelper& Helper) : Super(Helper) {}
AVoxelLandWorld::~AVoxelLandWorld() = default;

void AVoxelLandWorld::BeginPlay()
{
	bLifecycleEnded = false;
	BindWorldTearDown();
	Super::BeginPlay();
}

void AVoxelLandWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	bLifecycleEnded = true;
	UnbindWorldTearDown();
	Shutdown();
	Super::EndPlay(Reason);
}

void AVoxelLandWorld::Destroyed()
{
	// EndPlay is not routed for an actor that never began play.
	bLifecycleEnded = true;
	UnbindWorldTearDown();
	Shutdown();
	Super::Destroyed();
}

void AVoxelLandWorld::BeginDestroy()
{
	// World or level teardown may bypass Destroyed and EndPlay. Do not call
	// Destroy on other UObjects from GC; world teardown owns those actors.
	bLifecycleEnded = true;
	UnbindWorldTearDown();
	ShutdownInternal(false);
	Super::BeginDestroy();
}

void AVoxelLandWorld::HandleWorldBeginTearDown(UWorld* World)
{
	if (World == GetWorld())
	{
		bLifecycleEnded = true;
		UnbindWorldTearDown();
		Shutdown();
	}
}

void AVoxelLandWorld::BindWorldTearDown()
{
	if (!WorldTearDownHandle.IsValid())
	{
		WorldTearDownHandle =
			FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &AVoxelLandWorld::HandleWorldBeginTearDown);
	}
}

void AVoxelLandWorld::UnbindWorldTearDown()
{
	if (WorldTearDownHandle.IsValid())
	{
		FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
		WorldTearDownHandle.Reset();
	}
}

void AVoxelLandWorld::Initialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator)
{
	TryInitialize(MoveTemp(Generator));
}

bool AVoxelLandWorld::GetM6WorldIdentity(TArray<uint8>& OutIdentity) const
{
	check(IsInGameThread());
	if (!State || State->M6Identity.IsEmpty())
	{
		OutIdentity.Reset();
		return false;
	}
	OutIdentity = State->M6Identity;
	return true;
}

bool AVoxelLandWorld::TryInitialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator,
	const FVoxelLandChunkKey* Startup, TArray<uint8> M6Identity)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
	    || World->bIsTearingDown)
	{
		return false;
	}
	if (!Generator)
	{
		LastInitializationError = TEXT("Generator is missing.");
		return false;
	}
	TUniquePtr<FVoxelLandWorldState> Candidate = MakeUnique<FVoxelLandWorldState>();
	Candidate->Generator = Generator;
	Candidate->M6Identity = MoveTemp(M6Identity);
	Candidate->bServer = bDataOnly || GetNetMode() == NM_DedicatedServer;
	Candidate->RegionDimensions = FIntVector(
		FMath::Clamp(RegionSize.X, 1, 32), FMath::Clamp(RegionSize.Y, 1, 32), FMath::Clamp(RegionSize.Z, 1, 8));
	if (Startup)
	{
		if (Startup->LOD != 0 || !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(
				Startup->Coordinate, Generator->GetProfile().VoxelSizeCm, Generator->IsM4Unlimited()))
		{
			LastInitializationError = TEXT("Startup chunk exceeds the supported coordinate range.");
			return false;
		}
		if (const FVoxelLandError Error = Generator->ValidateM4ChunkDemand(*Startup))
		{
			LastInitializationError = FString::Printf(TEXT("Startup chunk is invalid: %s"), *Error.Message);
			return false;
		}
		FRecord& Record = Candidate->Records.Add(*Startup);
		Record.bExplicit = true;
		Record.bQueued = true;
		Candidate->bDemandComplete = false;
	}
	const int32 Count = FMath::Clamp(WorkerCount, 1, FWorker::MaxWorkers);
	for (int32 I = 0; I < Count; ++I)
	{
		auto Worker = MakeUnique<FWorker>(Candidate->Work);
		FRunnableThread* Thread = nullptr;
#if WITH_DEV_AUTOMATION_TESTS
		if (!bFailWorkerStartForTests)
#endif
		{
			Thread = FRunnableThread::Create(Worker.Get(), *FString::Printf(TEXT("VoxelLandWorker%d"), I));
		}
		if (!Thread)
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!bFailWorkerStartForTests)
#endif
			{
				UE_LOG(LogTemp, Error, TEXT("VoxelLand: unable to start worker"));
			}
			continue;
		}
		Candidate->Workers.Add(MoveTemp(Worker));
		Candidate->Threads.Add(Thread);
	}
	if (Candidate->Threads.IsEmpty())
	{
		LastInitializationError = TEXT("Unable to start terrain workers");
		return false;
	}
	const uint64 PreviousEpoch = ConfigurationEpoch;
	Shutdown();
	if (ConfigurationEpoch != PreviousEpoch + 1 || bLifecycleEnded || IsActorBeingDestroyed()
		|| !GetWorld() || GetWorld()->bIsTearingDown)
	{
		Candidate->Work->bStop.Store(true);
		for (int32 Index = 0; Index < Candidate->Threads.Num(); ++Index)
		{
			Candidate->Work->Wake->Trigger();
		}
		for (FRunnableThread* Thread : Candidate->Threads)
		{
			Thread->WaitForCompletion();
			delete Thread;
		}
		return false;
	}
	BindWorldTearDown();
	State = MoveTemp(Candidate);
	SetActorTickEnabled(true);
	return true;
}

void AVoxelLandWorld::Shutdown()
{
	ShutdownInternal(true);
}

void AVoxelLandWorld::ShutdownInternal(bool bDestroyRegionActors)
{
	check(IsInGameThread());
	SetActorTickEnabled(false);
	M4RejectedCameraDemandCount = 0;
	M4FirstCameraDemandRejection.Reset();
	++ConfigurationEpoch;
	StopDemo();
	StopPendingInitialization();
	if (!State)
	{
		return;
	}
	TUniquePtr<FVoxelLandWorldState> Retired = MoveTemp(State);
	TArray<TStrongObjectPtr<AVoxelLandRegion>> RetiredRegions;
	if (bDestroyRegionActors)
	{
		RetiredRegions.Reserve(Regions.Num());
		for (AVoxelLandRegion* Region : Regions)
		{
			if (IsValid(Region))
			{
				RetiredRegions.Emplace(Region);
			}
		}
	}
	Regions.Empty();
	++Retired->Epoch;
	for (auto& Pair : Retired->Records)
	{
		if (Pair.Value.Cancel)
		{
			Pair.Value.Cancel->bCancelled.Store(true);
		}
	}
	Retired->Work->bStop.Store(true);
	for (int32 I = 0; I < Retired->Threads.Num(); ++I)
	{
		Retired->Work->Wake->Trigger();
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (ShutdownHookForTests)
	{
		TFunction<void()> Hook = MoveTemp(ShutdownHookForTests);
		Hook();
	}
#endif
	for (const TStrongObjectPtr<AVoxelLandRegion>& Region : RetiredRegions)
	{
		if (IsValid(Region.Get()))
		{
			Region->SetActorHiddenInGame(true);
			Region->Destroy();
		}
	}
	for (FRunnableThread* Thread : Retired->Threads)
	{
		Thread->WaitForCompletion();
		delete Thread;
	}
}

bool AVoxelLandWorld::RequestChunk(const FVoxelLandChunkKey& Key)
{
	check(IsInGameThread());
	if (!State || Key.LOD != 0
		|| !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(
			Key.Coordinate, State->Generator->GetProfile().VoxelSizeCm, State->Generator->IsM4Unlimited())
		|| (State->Records.Num() >= FMath::Clamp(MaxResidentChunks, 1, FWorker::MaxRecords)
			&& !State->Records.Contains(Key)))
	{
		return false;
	}
	if (const FVoxelLandError DemandError = State->Generator->ValidateM4ChunkDemand(Key))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand rejected chunk (%d,%d,%d): %s"), Key.Coordinate.X, Key.Coordinate.Y,
			Key.Coordinate.Z, *DemandError.Message);
		return false;
	}
	FRecord& Record = State->Records.FindOrAdd(Key);
	Record.bExplicit = true;
	Record.bFailed = false;
	Record.Error.Reset();
	State->bDemandComplete = false;
	if (!Record.Data && !Record.bRunning)
	{
		Record.bQueued = true;
	}
	return true;
}

bool AVoxelLandWorld::RequestDataChunk(FIntVector Coordinate)
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = Coordinate;
	return RequestChunk(Key);
}

void AVoxelLandWorld::ReleaseDataChunk(FIntVector Coordinate)
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = Coordinate;
	ReleaseChunk(Key);
}

bool AVoxelLandWorld::HasDataChunk(FIntVector Coordinate) const
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = Coordinate;
	return FindChunk(Key).IsValid();
}

bool AVoxelLandWorld::GetDataChunkError(FIntVector Coordinate, FString& OutError) const
{
	check(IsInGameThread());
	OutError.Reset();
	if (!State)
	{
		return false;
	}
	FVoxelLandChunkKey Key;
	Key.Coordinate = Coordinate;
	const FRecord* Record = State->Records.Find(Key);
	if (!Record || !Record->bFailed)
	{
		return false;
	}
	OutError = Record->Error;
	return true;
}

void AVoxelLandWorld::ReleaseChunk(const FVoxelLandChunkKey& Key)
{
	check(IsInGameThread());
	if (!State)
	{
		return;
	}
	if (FRecord* Record = State->Records.Find(Key))
	{
		Record->bExplicit = false;
		State->bDemandComplete = false;
	}
}

TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> AVoxelLandWorld::FindChunk(
	const FVoxelLandChunkKey& Key) const
{
	check(IsInGameThread());
	if (!State)
	{
		return nullptr;
	}
	if (const FRecord* Record = State->Records.Find(Key))
	{
		return Record->Data;
	}
	return nullptr;
}

void AVoxelLandWorld::SetRegionHidden(FIntVector Region, bool bRegionHidden)
{
	check(IsInGameThread());
	if (!State)
	{
		return;
	}
	if (bRegionHidden)
	{
		if (State->HiddenRegions.Num() >= FWorker::MaxRecords && !State->HiddenRegions.Contains(Region))
		{
			return;
		}
		State->HiddenRegions.Add(Region);
	}
	else
	{
		State->HiddenRegions.Remove(Region);
	}
	if (const TWeakObjectPtr<AVoxelLandRegion>* Found = State->RegionMap.Find(Region))
	{
		if (Found->IsValid())
		{
			Found->Get()->SetActorHiddenInGame(bRegionHidden);
		}
	}
}

void AVoxelLandWorld::SetTerrainMaterial(UMaterialInterface* Material)
{
	check(IsInGameThread());
	TerrainMaterial = Material;
	for (AVoxelLandRegion* Region : Regions)
	{
		if (IsValid(Region))
		{
			Region->SetTerrainMaterial(Material);
		}
	}
}
#if WITH_DEV_AUTOMATION_TESTS
void AVoxelLandWorld::SetNextTestBarrier(TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> Barrier)
{
	check(IsInGameThread());
	if (State)
	{
		State->NextBarrier = MoveTemp(Barrier);
	}
}

uint64 AVoxelLandWorld::GetStaleCompletionCount() const
{
	return State ? State->Stale : 0;
}

void AVoxelLandWorld::SetTestViewLocation(FVector Location)
{
	if (State)
	{
		State->TestView = Location;
	}
}

void AVoxelLandWorld::SetTestViewVelocity(FVector Velocity)
{
	if (State)
	{
		State->ViewVelocity = Velocity;
	}
}

int32 AVoxelLandWorld::GetDemandedChunkCount() const
{
	int32 Count = 0;
	if (State)
	{
		for (const auto& Pair : State->Records)
		{
			Count += Pair.Value.bDemand ? 1 : 0;
		}
	}
	return Count;
}

bool AVoxelLandWorld::IsExplicitChunkForTests(const FVoxelLandChunkKey& Key) const
{
	const FRecord* Record = State ? State->Records.Find(Key) : nullptr;
	return Record && Record->bExplicit && Record->bQueued;
}

bool AVoxelLandWorld::IsChunkDemanded(FIntVector Coordinate) const
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = Coordinate;
	const FRecord* R = State ? State->Records.Find(Key) : nullptr;
	return R && R->bDemand;
}
#endif
void AVoxelLandWorld::DumpStats() const
{
	if (!State)
	{
		UE_LOG(LogTemp, Display, TEXT("VoxelLand inactive"));
		return;
	}
	int32 Queued = 0, Ready = 0, Vertices = 0, Triangles = 0;
	int64 FieldBytes = 0, MeshBytes = 0;
	for (const auto& Pair : State->Records)
	{
		const FRecord& R = Pair.Value;
		Queued += R.bQueued ? 1 : 0;
		Ready += R.Data ? 1 : 0;
		if (R.Data)
		{
			FieldBytes += R.Data->Density.GetAllocatedSize();
		}
		MeshBytes += R.MeshBytes;
		Vertices += R.Vertices;
		Triangles += R.Triangles;
	}
	UE_LOG(LogTemp, Display,
		TEXT(
			"VoxelLand server=%d requested=%d queued=%d outstanding=%d ready=%d regions=%d vertices=%d triangles=%d fieldBytes=%lld meshBytes=%lld cancelled=%llu stale=%llu failed=%llu"
		),
		State->bServer, State->Records.Num(), Queued, State->Active, Ready, Regions.Num(), Vertices, Triangles,
		FieldBytes, MeshBytes, State->Cancelled, State->Stale, State->Failed);
}

void AVoxelLandWorld::UpdateDemand()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_UpdateDemand);
	if (!State || State->bServer)
	{
		return;
	}
	FVector Location;
#if WITH_DEV_AUTOMATION_TESTS
	if (State->TestView.IsSet())
	{
		Location = State->TestView.GetValue();
	}
	else
#endif
	{
		APlayerController* Controller = GetWorld()->GetFirstPlayerController();
		if (!Controller || !Controller->PlayerCameraManager)
		{
			return;
		}
		Location = Controller->PlayerCameraManager->GetCameraLocation();
		State->ViewVelocity = Controller->GetPawn() ? Controller->GetPawn()->GetVelocity() : FVector::ZeroVector;
	}
	FVector GlobalLocation;
	if (!GetWorld() || !VoxelLandWorldCoordinates::ToGlobal(Location, GetWorld()->OriginLocation, GlobalLocation))
	{
		return;
	}
	const double Width = double(State->Generator->GetProfile().VoxelSizeCm) * 32.0;
	const FVector Position = GlobalLocation / Width;
	constexpr double CoordinateLimit = double(MAX_int32) - 32.0;
	if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z)
	    || FMath::Abs(Position.X) > CoordinateLimit || FMath::Abs(Position.Y) > CoordinateLimit
	    || FMath::Abs(Position.Z) > CoordinateLimit)
	{
		return;
	}
	const FIntVector NewChunk(
		FMath::FloorToInt(Position.X), FMath::FloorToInt(Position.Y), FMath::FloorToInt(Position.Z));
	const int32 H = FMath::Clamp(HorizontalRadius, 1, 8) + 1, V = FMath::Clamp(VerticalRadius, 1, 4) + 1;
	const int32 Limit = FMath::Clamp(MaxResidentChunks, 1, FWorker::MaxRecords);
	const FVector Direction = State->ViewVelocity.GetSafeNormal();
	auto SectorAxis = [](double Value) {
		return Value > 0.25 ? 1 : Value < -0.25 ? -1 : 0;
	};
	const FIntVector VelocitySector(SectorAxis(Direction.X), SectorAxis(Direction.Y), SectorAxis(Direction.Z));
	if (State->bDemandComplete && State->bHasCamera && State->CameraChunk == NewChunk && State->LastHorizontal == H
	    && State->LastVertical == V && State->LastLimit == Limit && State->LastVelocitySector == VelocitySector)
	{
		return;
	}
	State->LastVelocitySector = VelocitySector;
	State->CameraChunk = NewChunk;
	State->bHasCamera = true;
	State->LastHorizontal = H;
	State->LastVertical = V;
	State->LastLimit = Limit;
	TArray<FVoxelLandChunkKey> Candidates;
	int32 RejectedM4Demand = 0;
	FString FirstM4Rejection;
	for (int32 Z = -V; Z <= V; ++Z)
	{
		for (int32 Y = -H; Y <= H; ++Y)
		{
			for (int32 X = -H; X <= H; ++X)
			{
				FVoxelLandChunkKey Key;
				Key.Coordinate = NewChunk + FIntVector(X, Y, Z);
				if (VoxelLandMeshCoordinates::IsKeyInPrecisionRange(
					Key.Coordinate, State->Generator->GetProfile().VoxelSizeCm, State->Generator->IsM4Unlimited()))
				{
					if (const FVoxelLandError Error = State->Generator->ValidateM4ChunkDemand(Key))
					{
						++RejectedM4Demand;
						if (FirstM4Rejection.IsEmpty())
						{
							FirstM4Rejection = FString::Printf(TEXT("Chunk (%d, %d, %d): %s"), Key.Coordinate.X,
								Key.Coordinate.Y, Key.Coordinate.Z, *Error.Message);
						}
					}
					else
					{
						Candidates.Add(Key);
					}
				}
				else if (State->Generator->IsM4Unlimited())
				{
					++RejectedM4Demand;
					if (FirstM4Rejection.IsEmpty())
					{
						FirstM4Rejection =
							FString::Printf(TEXT("Chunk (%d, %d, %d) exceeds the M4 CPU meshing coordinate envelope"),
								Key.Coordinate.X, Key.Coordinate.Y, Key.Coordinate.Z);
					}
				}
			}
		}
	}
	M4RejectedCameraDemandCount = RejectedM4Demand;
	M4FirstCameraDemandRejection = MoveTemp(FirstM4Rejection);
	if (RejectedM4Demand > 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("VoxelLand skipped %d camera-demand chunks outside supported M4 bounds"),
			RejectedM4Demand);
	}
	const FVector Forward = State->ViewVelocity.GetSafeNormal();
	Candidates.Sort([NewChunk, Forward](const FVoxelLandChunkKey& A, const FVoxelLandChunkKey& B) {
		const FIntVector DA = A.Coordinate - NewChunk, DB = B.Coordinate - NewChunk;
		const int32 DistA = DA.X * DA.X + DA.Y * DA.Y + DA.Z * DA.Z, DistB = DB.X * DB.X + DB.Y * DB.Y + DB.Z * DB.Z;
		if (DistA != DistB)
		{
			return DistA < DistB;
		}
		const double AheadA = FVector::DotProduct(FVector(DA), Forward),
		             AheadB = FVector::DotProduct(FVector(DB), Forward);
		if (AheadA != AheadB)
		{
			return AheadA > AheadB;
		}
		return A.Coordinate.X != B.Coordinate.X
			       ? A.Coordinate.X < B.Coordinate.X
			       : (A.Coordinate.Y != B.Coordinate.Y
				          ? A.Coordinate.Y < B.Coordinate.Y
				          : A.Coordinate.Z < B.Coordinate.Z);
	});
	int32 ExplicitCount = 0;
	for (auto& Pair : State->Records)
	{
		Pair.Value.bDemand = false;
		ExplicitCount += Pair.Value.bExplicit ? 1 : 0;
	}
	int32 Available = FMath::Max(0, Limit - ExplicitCount);
	TSet<FVoxelLandChunkKey> Selected;
	for (const auto& Key : Candidates)
	{
		FRecord* Existing = State->Records.Find(Key);
		if (Existing && Existing->bExplicit)
		{
			Existing->bDemand = true;
			continue;
		}
		if (Available > 0)
		{
			Selected.Add(Key);
			--Available;
		}
	}
	int32 Missing = 0;
	for (const auto& Key : Selected)
	{
		if (!State->Records.Contains(Key))
		{
			++Missing;
		}
	}
	int32 NeedToFree = FMath::Max(0, State->Records.Num() + Missing - Limit);
	TArray<FVoxelLandChunkKey> Evictions;
	for (const auto& Pair : State->Records)
	{
		if (!Pair.Value.bExplicit && !Selected.Contains(Pair.Key))
		{
			Evictions.Add(Pair.Key);
		}
	}
	Evictions.Sort([NewChunk](const FVoxelLandChunkKey& A, const FVoxelLandChunkKey& B) {
		const int64 DistA = FMath::Abs(int64(A.Coordinate.X) - NewChunk.X)
		                    + FMath::Abs(int64(A.Coordinate.Y) - NewChunk.Y)
		                    + FMath::Abs(int64(A.Coordinate.Z) - NewChunk.Z);
		const int64 DistB = FMath::Abs(int64(B.Coordinate.X) - NewChunk.X)
		                    + FMath::Abs(int64(B.Coordinate.Y) - NewChunk.Y)
		                    + FMath::Abs(int64(B.Coordinate.Z) - NewChunk.Z);
		if (DistA != DistB)
		{
			return DistA > DistB;
		}
		return A.Coordinate.X != B.Coordinate.X
			       ? A.Coordinate.X > B.Coordinate.X
			       : (A.Coordinate.Y != B.Coordinate.Y
				          ? A.Coordinate.Y > B.Coordinate.Y
				          : A.Coordinate.Z > B.Coordinate.Z);
	});
	for (const auto& Key : Evictions)
	{
		if (NeedToFree <= 0)
		{
			break;
		}
		RetireChunk(Key);
		--NeedToFree;
	}
	for (const auto& Key : Candidates)
	{
		if (!Selected.Contains(Key))
		{
			continue;
		}
		if (State->Records.Num() >= Limit && !State->Records.Contains(Key))
		{
			continue;
		}
		FRecord& Record = State->Records.FindOrAdd(Key);
		Record.bDemand = true;
		if (!Record.Data && !Record.bRunning && !Record.bFailed)
		{
			Record.bQueued = true;
		}
	}
	// Capacity changes and explicit requests invalidate this cache. A stable cap
	// does not cause the full candidate set to be rebuilt every frame.
	State->bDemandComplete = true;
}

void AVoxelLandWorld::PumpCompletions()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_Publish);
	if (!State)
	{
		return;
	}
	const double Deadline = FPlatformTime::Seconds() + 0.001;
	FCompletion Done;
	int32 Published = 0;
	while (Published < 2 && FPlatformTime::Seconds() < Deadline && State->Work->Completed.Dequeue(Done))
	{
		++Published;
		--State->Active;
		FRecord* Record = State->Records.Find(Done.Job.Key);
		if (!VoxelLandPublication::IsCurrent(Record != nullptr, Done.Job.Epoch, State->Epoch, Done.Job.Ticket,
			Record ? Record->Ticket : 0, Done.Job.Generator == State->Generator, true))
		{
			++State->Stale;
			continue;
		}
		Record->bRunning = false;
		Record->Cancel.Reset();
		if (Done.bCancelled || (!Record->bDemand && !Record->bExplicit))
		{
			++State->Cancelled;
			continue;
		}
		if (Done.Error)
		{
			Record->bFailed = true;
			Record->Error = Done.Error.Message.IsEmpty()
				                ? FString::Printf(TEXT("Chunk generation failed (error code %d)"),
					                int32(Done.Error.Code))
				                : Done.Error.Message.Left(1024);
			++State->Failed;
			UE_LOG(LogTemp, Error, TEXT("VoxelLand chunk (%d,%d,%d): %s"), Done.Job.Key.Coordinate.X,
				Done.Job.Key.Coordinate.Y, Done.Job.Key.Coordinate.Z, *Done.Error.Message);
			continue;
		}
		Record->Data = Done.Data;
		if (!Done.Mesh || State->bServer)
		{
			continue;
		}
		const FIntVector RegionKey = VoxelLandCoordinates::RegionOf(Done.Job.Key.Coordinate, State->RegionDimensions);
		AVoxelLandRegion* Region = State->RegionMap.FindRef(RegionKey).Get();
		if (!Region)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Region =
				GetWorld()->SpawnActor<AVoxelLandRegion>(AVoxelLandRegion::StaticClass(), FTransform::Identity, Params);
			if (!Region)
			{
				Record->Data.Reset();
				Record->bFailed = true;
				Record->Error = TEXT("VoxelLand region spawn failed");
				++State->Failed;
				UE_LOG(LogTemp, Error, TEXT("VoxelLand region spawn failed"));
				continue;
			}
			Regions.Add(Region);
			State->RegionMap.Add(RegionKey, Region);
			Region->SetActorHiddenInGame(State->HiddenRegions.Contains(RegionKey));
		}
		const FIntVector C = Done.Job.Key.Coordinate;
		if (!Region->Publish(C, VoxelLandMeshCoordinates::RenderOrigin(C, State->Generator->GetProfile().VoxelSizeCm),
			Done.Mesh, TerrainMaterial))
		{
			Record->Data.Reset();
			Record->bFailed = true;
			Record->Error = TEXT("VoxelLand component rejected mesh");
			++State->Failed;
			UE_LOG(LogTemp, Error, TEXT("VoxelLand component rejected mesh for (%d,%d,%d)"), C.X, C.Y, C.Z);
		}
		else
		{
			Record->Vertices = Done.Mesh->Vertices.Num();
			Record->Triangles = Done.Mesh->Indices.Num() / 3;
			Record->MeshBytes = Done.Mesh->Vertices.GetAllocatedSize() + Done.Mesh->Indices.GetAllocatedSize();
		}
	}
}

void AVoxelLandWorld::Dispatch()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_Dispatch);
	if (!State)
	{
		return;
	}
	// At most one outstanding job per worker, including undrained completions. This bounds
	// result memory by worker count times the maximum 34^3 vertices and 3*32^3 quads.
	while (State->Active < State->Threads.Num())
	{
		FRecord* Best = nullptr;
		FVoxelLandChunkKey BestKey;
		int64 BestDistance = MAX_int64;
		double BestAhead = -TNumericLimits<double>::Max();
		const FVector Forward = State->ViewVelocity.GetSafeNormal();
		for (auto& Pair : State->Records)
		{
			FRecord& R = Pair.Value;
			if (!R.bQueued || R.bRunning || R.bFailed || (!R.bDemand && !R.bExplicit))
			{
				continue;
			}
			const FIntVector C = Pair.Key.Coordinate;
			const int64 DX = int64(C.X) - State->CameraChunk.X, DY = int64(C.Y) - State->CameraChunk.Y,
			            DZ = int64(C.Z) - State->CameraChunk.Z;
			// Clamp to avoid overflow for explicit far-away requests.
			const int64 Distance = FMath::Min<int64>(FMath::Abs(DX), 1000000)
			                       + FMath::Min<int64>(FMath::Abs(DY), 1000000)
			                       + FMath::Min<int64>(FMath::Abs(DZ), 1000000);
			const double Ahead = DX * Forward.X + DY * Forward.Y + DZ * Forward.Z;
			const FIntVector B = BestKey.Coordinate;
			if (!Best || Distance < BestDistance
			    || (Distance == BestDistance
			        && (Ahead > BestAhead
			            || (Ahead == BestAhead
			                && (C.X < B.X || (C.X == B.X && (C.Y < B.Y || (C.Y == B.Y && C.Z < B.Z))))))))
			{
				Best = &R;
				BestKey = Pair.Key;
				BestDistance = Distance;
				BestAhead = Ahead;
			}
		}
		if (!Best)
		{
			break;
		}
		FJob Job;
		Job.Key = BestKey;
		Job.Generator = State->Generator;
		Job.Epoch = State->Epoch;
		Job.Ticket = ++State->NextTicket;
		Job.bRender = !State->bServer;
		Job.Cancel = MakeShared<FCancel, ESPMode::ThreadSafe>();
#if WITH_DEV_AUTOMATION_TESTS
		Job.Barrier = MoveTemp(State->NextBarrier);
#endif
		Best->Ticket = Job.Ticket;
		Best->Cancel = Job.Cancel;
		Best->bRunning = true;
		Best->bQueued = false;
		{
			FScopeLock Lock(&State->Work->Mutex);
			State->Work->Jobs.Enqueue(MoveTemp(Job));
		}
		++State->Active;
		State->Work->Wake->Trigger();
	}
}

void AVoxelLandWorld::RetireChunk(const FVoxelLandChunkKey& Key)
{
	FRecord* R = State ? State->Records.Find(Key) : nullptr;
	if (!R)
	{
		return;
	}
	if (R->Cancel)
	{
		R->Cancel->bCancelled.Store(true);
	}
	const FIntVector RegionKey = VoxelLandCoordinates::RegionOf(Key.Coordinate, State->RegionDimensions);
	if (TWeakObjectPtr<AVoxelLandRegion>* Weak = State->RegionMap.Find(RegionKey))
	{
		if (AVoxelLandRegion* Region = Weak->Get())
		{
			Region->Remove(Key.Coordinate);
			if (Region->IsEmpty())
			{
				Region->Destroy();
				Regions.Remove(Region);
				State->RegionMap.Remove(RegionKey);
			}
		}
	}
	State->Records.Remove(Key);
	State->bDemandComplete = false;
}

void AVoxelLandWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PollInitialization();
	PollDemo();
	if (!State)
	{
		return;
	}
	UpdateDemand();
	TArray<FVoxelLandChunkKey> Retire;
	int32 Queued = 0, Ready = 0, Visible = 0, Vertices = 0, Triangles = 0;
	int64 FieldBytes = 0, MeshBytes = 0;
	const int32 H = FMath::Clamp(HorizontalRadius, 1, 8) + 2, V = FMath::Clamp(VerticalRadius, 1, 4) + 2;
	for (const auto& Pair : State->Records)
	{
		const FRecord& R = Pair.Value;
		Queued += R.bQueued ? 1 : 0;
		Ready += R.Data ? 1 : 0;
		if (R.Data)
		{
			FieldBytes += R.Data->Density.GetAllocatedSize();
		}
		MeshBytes += R.MeshBytes;
		Vertices += R.Vertices;
		Triangles += R.Triangles;
		if (R.MeshBytes > 0
		    && !State->HiddenRegions.Contains(
			    VoxelLandCoordinates::RegionOf(Pair.Key.Coordinate, State->RegionDimensions)))
		{
			++Visible;
		}
		if (R.bExplicit || R.bDemand)
		{
			continue;
		}
		const FIntVector C = Pair.Key.Coordinate, Camera = State->CameraChunk;
		const bool bRetain = !State->bServer && State->bHasCamera && FMath::Abs(int64(C.X) - Camera.X) <= H
		                     && FMath::Abs(int64(C.Y) - Camera.Y) <= H && FMath::Abs(int64(C.Z) - Camera.Z) <= V;
		if (!bRetain)
		{
			Retire.Add(Pair.Key);
		}
	}
	for (const FVoxelLandChunkKey& Key : Retire)
	{
		RetireChunk(Key);
	}
	PumpCompletions();
	Dispatch();
	// Counters report the latest ticked VoxelLand world; GPU allocations and deferred
	// releases require Unreal's renderer/RHI memory tools.
	TRACE_COUNTER_SET(VoxelQueued, Queued);
	TRACE_COUNTER_SET(VoxelOutstanding, State->Active);
	TRACE_COUNTER_SET(VoxelReady, Ready);
	TRACE_COUNTER_SET(VoxelPrimitives, Visible);
	TRACE_COUNTER_SET(VoxelRegions, Regions.Num());
	TRACE_COUNTER_SET(VoxelVertices, Vertices);
	TRACE_COUNTER_SET(VoxelTriangles, Triangles);
	TRACE_COUNTER_SET(VoxelStale, State->Stale);
	TRACE_COUNTER_SET(VoxelCancelled, State->Cancelled);
	TRACE_COUNTER_SET(VoxelFieldBytes, FieldBytes);
	TRACE_COUNTER_SET(VoxelMeshBytes, MeshBytes);
}

class FVoxelLandDemoJob
{
public:
	TAtomic<bool> Cancelled{false};
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FVoxelLandError Error;
};

void AVoxelLandWorld::StartDemo()
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
	    || World->bIsTearingDown || !HasActorBegunPlay())
	{
		return;
	}
	Shutdown();
	const int32 DemoSeed = Seed;
	const float Size = VoxelSizeCm;
	const TSharedRef<FVoxelLandDemoJob, ESPMode::ThreadSafe> Job = MakeShared<FVoxelLandDemoJob, ESPMode::ThreadSafe>();
	PendingDemo = Job;
	SetActorTickEnabled(true);
	DemoTask = Async(EAsyncExecution::ThreadPool, [Job, DemoSeed, Size]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 10;
		Draft.Constants = {0.001f, 3.f, 0.5f, 2.f, 1400.f, 0.0015f, 2000.f, 250.f};
		FVoxelLandSeedSlot SeedSlot;
		SeedSlot.NodeId = FGuid(0x12589abc, 0x34127890, 0xabcdef01, 0x10203040);
		Draft.Seeds.Add(SeedSlot);
		FVoxelLandNoiseConfig Height;
		Height.Frequency.Index = 0;
		Height.Octaves.Index = 1;
		Height.Gain.Index = 2;
		Height.Lacunarity.Index = 3;
		FVoxelLandNoiseConfig Caves = Height;
		Caves.Frequency.Index = 5;
		Draft.Noise = {Height, Caves};
		auto Op = [](EVoxelLandOpcode Code, uint16 Dst, uint16 A = 0, uint16 B = 0, uint16 C = 0, uint32 Imm = 0) {
			FVoxelLandInstruction I;
			I.Op = Code;
			I.Dst = Dst;
			I.A = A;
			I.B = B;
			I.C = C;
			I.Imm = Imm;
			return I;
		};
		Draft.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1),
		                      Op(EVoxelLandOpcode::LoadZ, 2), Op(EVoxelLandOpcode::Fbm2, 3, 0, 1, 2, 0),
		                      Op(EVoxelLandOpcode::LoadConstant, 4, 0, 0, 0, 4),
		                      Op(EVoxelLandOpcode::Multiply, 3, 3, 4),
		                      Op(EVoxelLandOpcode::Subtract, 3, 2, 3), Op(EVoxelLandOpcode::Fbm3, 5, 0, 1, 2, 1),
		                      Op(EVoxelLandOpcode::Abs, 5, 5), Op(EVoxelLandOpcode::LoadConstant, 6, 0, 0, 0, 6),
		                      Op(EVoxelLandOpcode::Multiply, 5, 5, 6),
		                      Op(EVoxelLandOpcode::LoadConstant, 7, 0, 0, 0, 7),
		                      Op(EVoxelLandOpcode::Subtract, 5, 7, 5), Op(EVoxelLandOpcode::Max, 8, 3, 5),
		                      Op(EVoxelLandOpcode::StoreDensity, 0, 8)};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Program);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (!Error && !Job->Cancelled.Load())
		{
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			Settings.WorldSeed = uint64(uint32(DemoSeed));
			Settings.Profile.VoxelSizeCm = Size;
			Error = FVoxelLandGenerator::Create(Settings, Generator);
		}
		Job->Generator = MoveTemp(Generator);
		Job->Error = MoveTemp(Error);
	});
}

void AVoxelLandWorld::StopDemo()
{
	if (PendingDemo)
	{
		PendingDemo->Cancelled.Store(true);
	}
	if (DemoTask.IsValid())
	{
		DemoTask.Wait();
		DemoTask = TFuture<void>();
	}
	PendingDemo.Reset();
}

void AVoxelLandWorld::PollDemo()
{
	if (!PendingDemo || !DemoTask.IsValid() || !DemoTask.IsReady())
	{
		return;
	}
	DemoTask.Wait();
	DemoTask = TFuture<void>();
	TSharedPtr<FVoxelLandDemoJob, ESPMode::ThreadSafe> Finished = MoveTemp(PendingDemo);
	if (Finished->Error)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelLand demo initialization failed: %s"), *Finished->Error.Message);
		SetActorTickEnabled(false);
		return;
	}
	Initialize(Finished->Generator);
}
