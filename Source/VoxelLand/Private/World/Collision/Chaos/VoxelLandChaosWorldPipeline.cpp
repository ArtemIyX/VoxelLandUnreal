#include "VoxelLandChaosWorld.h"

#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Engine/World.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Meshing/VoxelLandChaosMeshing.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Tasks/Task.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponentHost.h"
#include "World/VoxelLandWorld.h"
#include "../../VoxelLandWorldCoordinates.h"

TRACE_DECLARE_INT_COUNTER(VoxelChaosMeshWorkers, TEXT("VoxelLand/Chaos/Mesh Workers"));
TRACE_DECLARE_INT_COUNTER(VoxelChaosCookWorkers, TEXT("VoxelLand/Chaos/Cooks"));
TRACE_DECLARE_INT_COUNTER(VoxelChaosActiveBodies, TEXT("VoxelLand/Chaos/Active Bodies"));

namespace VoxelLandChaosPipeline
{
constexpr int64 BytesPerBuildingMesh = 4 * 1024 * 1024;

int64 MeshBytes(const FVoxelLandChaosMesh& Mesh)
{
	return int64(Mesh.Vertices.GetAllocatedSize()) + Mesh.Indices.GetAllocatedSize();
}

int64 EstimateCookBytes(const FVoxelLandChaosMesh& Mesh)
{
	// Reserve for the immutable provider payload and the engine's copied cook input and acceleration structures.
	return MeshBytes(Mesh) * 4;
}
}

void FVoxelLandChaosWorld::Invalidate(FVoxelLandChaosTile& Tile, const bool bSourceChanged)
{
	if (Tile.MeshCancellation)
	{
		Tile.MeshCancellation->bCancelled.Store(true);
		Tile.MeshCancellation.Reset();
	}
	Tile.Ticket = ++Ticket;
	Tile.Attempts = 0;
	Tile.RetryTime = 0.0;
	Tile.Phase = EVoxelLandChaosPhase::Field;
	Tile.bInstallOverBudget = false;
	Tile.Mesh.Reset();
	Tile.Cooked.Reset();
	if (bSourceChanged)
	{
		Tile.bHasCoverage = false;
		Tile.bRetireStaleBody = Tile.Body.IsValid();
	}
}

void FVoxelLandChaosWorld::RecordFailure(FVoxelLandChaosTile& Tile)
{
	++Failures;
	++Tile.Attempts;
	Tile.RetryTime = FPlatformTime::Seconds() + 0.25 * Tile.Attempts;
	Tile.Phase = EVoxelLandChaosPhase::Failed;
	Tile.Mesh.Reset();
	Tile.Cooked.Reset();
}

void FVoxelLandChaosWorld::DrainCompletions()
{
	FVoxelLandChaosMeshCompletion MeshDone;
	while (MeshCompletions->Dequeue(MeshDone))
	{
		--MeshActive;
		FVoxelLandChaosTile* Tile = Tiles.Find(MeshDone.Key);
		if (!bEnabled || !Tile || Tile->Owners == 0 || Tile->Ticket != MeshDone.Ticket
			|| Tile->Phase != EVoxelLandChaosPhase::Mesh)
		{
			++StaleResults;
			continue;
		}
		Tile->MeshCancellation.Reset();
		if (!MeshDone.Mesh || VoxelLandChaosPipeline::MeshBytes(*MeshDone.Mesh) > Settings.MaxQueuedMeshBytes)
		{
			RecordFailure(*Tile);
			continue;
		}
		Tile->Mesh = MoveTemp(MeshDone.Mesh);
		Tile->Phase = Tile->Mesh->Indices.IsEmpty() ? EVoxelLandChaosPhase::Apply : EVoxelLandChaosPhase::CookQueued;
	}

	FVoxelLandChaosCookCompletion CookDone;
	while (CookCompletions->Dequeue(CookDone))
	{
		int64 ReservedBytes = 0;
		if (!PendingCookTickets.RemoveAndCopyValue(CookDone.Ticket, ReservedBytes))
		{
			++StaleResults;
			continue;
		}
		--CookActive;
		CookBytes -= ReservedBytes;
		FVoxelLandChaosTile* Tile = Tiles.Find(CookDone.Key);
		if (!bEnabled || !Tile || Tile->Owners == 0 || Tile->Ticket != CookDone.Ticket
			|| Tile->Phase != EVoxelLandChaosPhase::Cooking)
		{
			++StaleResults;
			continue;
		}
		if (!CookDone.Result)
		{
			RecordFailure(*Tile);
			continue;
		}
		Tile->Cooked = MoveTemp(CookDone.Result);
		Tile->Phase = EVoxelLandChaosPhase::Apply;
	}
}

void FVoxelLandChaosWorld::RefreshDemand()
{
	const double Now = FPlatformTime::Seconds();
	for (TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		FVoxelLandChaosTile& Tile = Entry.Value;
		if (Tile.Owners == 0)
		{
			continue;
		}
		if (Settings.bMapByRenderLod)
		{
			const int32 VisualLod = World.ResolveChaosRenderLod(Entry.Key);
			if (VisualLod != Tile.ObservedVisualLod)
			{
				Tile.ObservedVisualLod = VisualLod;
				Tile.ObservedVisualLodTime = Now;
			}
			constexpr double VisualLodGraceSeconds = 0.25;
			if (!Tile.bHasVisualLod || Now - Tile.ObservedVisualLodTime >= VisualLodGraceSeconds)
			{
				Tile.StableVisualLod = VisualLod;
				Tile.bHasVisualLod = true;
			}
		}
		const EVoxelLandChaosCollisionDetail Detail = ResolveDetail(Entry.Key);
		if (Detail != Tile.Detail)
		{
			Invalidate(Tile, Detail == EVoxelLandChaosCollisionDetail::Disabled);
			Tile.Detail = Detail;
			bPinsNeedRefresh = true;
			NextPinsRetry = 0.0;
		}
		else if (Tile.bHasCoverage && Tile.ResidentBytes > 0 && !Tile.Body.IsValid())
		{
			Invalidate(Tile, true);
		}
	}
	if (bPinsNeedRefresh && Now >= NextPinsRetry)
	{
		bPinsNeedRefresh = false;
		for (TPair<FGuid, FVoxelLandChaosLease>& Entry : Leases)
		{
			bPinsNeedRefresh |= !SyncPins(Entry.Value);
		}
		NextPinsRetry = Now + 0.25;
	}
}

void FVoxelLandChaosWorld::SubmitMesh(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Field)
{
	const float VoxelSize = World.State->Generator->GetProfile().VoxelSizeCm;
	const bool bUnlimited = World.State->Generator->IsM4Unlimited();
	const uint8 Lod = static_cast<uint8>(FVoxelLandChaosCollisionSettings::GetLod(Tile.Detail));
	const uint64 JobTicket = Tile.Ticket;
	const TWeakPtr<FMeshQueue, ESPMode::ThreadSafe> WeakQueue = MeshCompletions;
	Tile.MeshCancellation = MakeShared<VoxelLandWorldPrivate::FCancel, ESPMode::ThreadSafe>();
	const TSharedPtr<VoxelLandWorldPrivate::FCancel, ESPMode::ThreadSafe> Cancellation = Tile.MeshCancellation;
	Tile.Phase = EVoxelLandChaosPhase::Mesh;
	++MeshActive;
	UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[WeakQueue, Key, JobTicket, Field = MoveTemp(Field), VoxelSize, bUnlimited, Lod, Cancellation]() {
			TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosExtractMesh);
			const TSharedRef<FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh =
				MakeShared<FVoxelLandChaosMesh, ESPMode::ThreadSafe>();
			const auto IsCancelled = [&WeakQueue, &Cancellation]() {
				return !WeakQueue.IsValid() || Cancellation->bCancelled.Load();
			};
			const EVoxelLandMeshResult Result =
				FVoxelLandChaosMeshing::Build(*Field, VoxelSize, Lod, *Mesh, IsCancelled, bUnlimited);
			if (const TSharedPtr<FMeshQueue, ESPMode::ThreadSafe> Queue = WeakQueue.Pin())
			{
				FVoxelLandChaosMeshCompletion Completion;
				Completion.Key = Key;
				Completion.Ticket = JobTicket;
				if (Result == EVoxelLandMeshResult::Success)
				{
					Completion.Mesh = Mesh;
				}
				Queue->Enqueue(MoveTemp(Completion));
			}
		},
		UE::Tasks::ETaskPriority::BackgroundNormal);
}

void FVoxelLandChaosWorld::SubmitCook(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosSubmitCook);
	const uint64 JobTicket = Tile.Ticket;
	const int64 Bytes = VoxelLandChaosPipeline::EstimateCookBytes(*Tile.Mesh);
	const TWeakPtr<FCookQueue, ESPMode::ThreadSafe> WeakQueue = CookCompletions;
	Tile.Phase = EVoxelLandChaosPhase::Cooking;
	++CookActive;
	CookBytes += Bytes;
	PendingCookTickets.Add(JobTicket, Bytes);
	FVoxelLandChaosCook::FCompletion Completion = [WeakQueue, Key, JobTicket](FVoxelLandChaosCookResultPtr Result) {
		TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosQueueCookCompletion);
		if (const TSharedPtr<FCookQueue, ESPMode::ThreadSafe> Queue = WeakQueue.Pin())
		{
			Queue->Enqueue(FVoxelLandChaosCookCompletion{Key, JobTicket, MoveTemp(Result)});
		}
	};
	const bool bAccepted =
#if WITH_DEV_AUTOMATION_TESTS
		CookHookForTests ? CookHookForTests(Tile.Mesh.ToSharedRef(), MoveTemp(Completion)) :
#endif
						 FVoxelLandChaosCook::Start(Tile.Mesh.ToSharedRef(), MoveTemp(Completion));
	if (!bAccepted)
	{
		PendingCookTickets.Remove(JobTicket);
		--CookActive;
		CookBytes -= Bytes;
		RecordFailure(Tile);
	}
}

AActor* FVoxelLandChaosWorld::GetBodyOwner()
{
	if (!BodyOwner.IsValid())
	{
		FActorSpawnParameters Parameters;
		Parameters.Owner = &World;
		Parameters.ObjectFlags |= RF_Transient;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BodyOwner = World.GetWorld()->SpawnActor<AVoxelLandChaosTerrainComponentHost>(
			AVoxelLandChaosTerrainComponentHost::StaticClass(), FTransform::Identity, Parameters);
	}
	return BodyOwner.Get();
}

bool FVoxelLandChaosWorld::Install(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosActivateBody);
	UVoxelLandChaosTerrainComponent* NewBody = nullptr;
	int64 Bytes = 0;
	if (Tile.Cooked)
	{
		Bytes = VoxelLandChaosPipeline::EstimateCookBytes(*Tile.Mesh);
		// The cooked replacement stays charged against pending-cook memory until activation.
		// The resident limit applies to the completed swap, so a full working set can rebuild.
		if (ResidentBytes - Tile.ResidentBytes + Bytes > Settings.MaxCookedBytes)
		{
			Tile.bInstallOverBudget = true;
			return false;
		}
		Tile.bInstallOverBudget = false;
		FVector Local;
		const FVector Logical =
			VoxelLandMeshCoordinates::RenderOrigin(Key.Coordinate, World.State->Generator->GetProfile().VoxelSizeCm);
		AActor* Host = GetBodyOwner();
		if (Host && VoxelLandWorldCoordinates::ToLocal(Logical, World.GetWorld()->OriginLocation, Local))
		{
			NewBody = UVoxelLandChaosTerrainComponent::Install(*Host, Local, *Tile.Cooked, BodySettings);
		}
		if (!NewBody)
		{
			RecordFailure(Tile);
			return true;
		}
	}
	// Replace and retire together before the physics step, keeping the old surface on install failure.
	if (UVoxelLandChaosTerrainComponent* Old = Tile.Body.Get())
	{
		Old->DestroyComponent();
	}
	ResidentBytes += Bytes - Tile.ResidentBytes;
	Tile.Body = NewBody;
	Tile.InstalledLod = NewBody ? static_cast<int32>(Tile.Mesh->EffectiveLod) : INDEX_NONE;
	Tile.ResidentBytes = Bytes;
	Tile.bHasCoverage = true;
	Tile.bRetireStaleBody = false;
	Tile.Attempts = 0;
	Tile.Phase = EVoxelLandChaosPhase::Ready;
	Tile.Mesh.Reset();
	Tile.Cooked.Reset();
	return true;
}

bool FVoxelLandChaosWorld::QueueRetirement(FVoxelLandChaosTile& Tile)
{
	const bool bHadBody = Tile.Body.IsValid();
	if (bHadBody)
	{
		RetiredBodies.Add({Tile.Body, Tile.ResidentBytes});
	}
	else
	{
		ResidentBytes -= Tile.ResidentBytes;
	}
	Tile.Body.Reset();
	Tile.InstalledLod = INDEX_NONE;
	Tile.ResidentBytes = 0;
	Tile.bHasCoverage = false;
	Tile.bRetireStaleBody = false;
	return bHadBody;
}

void FVoxelLandChaosWorld::AdvanceRetirements(const double Deadline, int32& PhysicsUpdates)
{
	while (!RetiredBodies.IsEmpty() && PhysicsUpdates < Settings.MaxPhysicsUpdatesPerTick
		   && FPlatformTime::Seconds() < Deadline)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosRetireBody);
		const FVoxelLandChaosRetirement Retired = RetiredBodies.Pop(EAllowShrinking::No);
		if (UVoxelLandChaosTerrainComponent* Body = Retired.Body.Get())
		{
			++PhysicsUpdates;
			Body->DestroyComponent();
		}
		ResidentBytes -= Retired.Bytes;
	}
}

int64 FVoxelLandChaosWorld::GetAwaitingBytes() const
{
	int64 Bytes =
		int64(MeshActive) * FMath::Min(Settings.MaxQueuedMeshBytes, VoxelLandChaosPipeline::BytesPerBuildingMesh);
	for (const TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		if (Entry.Value.Mesh && Entry.Value.Phase != EVoxelLandChaosPhase::Cooking)
		{
			Bytes += VoxelLandChaosPipeline::MeshBytes(*Entry.Value.Mesh);
		}
	}
	return Bytes;
}

int64 FVoxelLandChaosWorld::GetCookedAwaitingInstallBytes() const
{
	int64 Bytes = 0;
	for (const TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		if (Entry.Value.Cooked && Entry.Value.Mesh)
		{
			Bytes += VoxelLandChaosPipeline::EstimateCookBytes(*Entry.Value.Mesh);
		}
	}
	return Bytes;
}

void FVoxelLandChaosWorld::AdvanceTile(
	const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile, const double Deadline, int32& PhysicsUpdates)
{
	if (Tile.bRetireStaleBody)
	{
		QueueRetirement(Tile);
	}
	if (Tile.Detail == EVoxelLandChaosCollisionDetail::Disabled || Tile.Owners == 0
		|| FPlatformTime::Seconds() >= Deadline)
	{
		return;
	}
	if (Tile.Phase == EVoxelLandChaosPhase::Failed && Tile.Attempts < MaxAttempts
		&& FPlatformTime::Seconds() >= Tile.RetryTime)
	{
		Tile.Ticket = ++Ticket;
		Tile.Phase = EVoxelLandChaosPhase::Field;
	}
	if (Tile.Phase == EVoxelLandChaosPhase::Field)
	{
		const VoxelLandWorldPrivate::FRecord* Field = World.State->Records.Find(Key);
		if (Field && !Field->Data && Field->bFailed
			&& Field->CollisionRetries >= VoxelLandWorldPrivate::FRecord::MaxCollisionRetries)
		{
			Tile.Attempts = MaxAttempts - 1;
			RecordFailure(Tile);
			return;
		}
	}
	if (Tile.Phase == EVoxelLandChaosPhase::Apply && PhysicsUpdates < Settings.MaxPhysicsUpdatesPerTick
		&& Install(Key, Tile))
	{
		++PhysicsUpdates;
	}
	else if (Tile.Phase == EVoxelLandChaosPhase::CookQueued && CookActive < Settings.MaxConcurrentCooks)
	{
		const int64 Bytes = VoxelLandChaosPipeline::EstimateCookBytes(*Tile.Mesh);
		if (Bytes > Settings.MaxPendingCookBytes || Bytes > Settings.MaxCookedBytes)
		{
			Tile.Attempts = MaxAttempts - 1;
			RecordFailure(Tile);
		}
		else if (CookBytes + GetCookedAwaitingInstallBytes() + Bytes <= Settings.MaxPendingCookBytes)
		{
			SubmitCook(Key, Tile);
		}
	}
	else if (Tile.Phase == EVoxelLandChaosPhase::Field && MeshActive < Settings.MaxMeshWorkers
			 && GetAwaitingBytes()
						+ FMath::Min(Settings.MaxQueuedMeshBytes, VoxelLandChaosPipeline::BytesPerBuildingMesh)
					<= Settings.MaxQueuedMeshBytes)
	{
		if (TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Field = World.FindChunk(Key))
		{
			int32 QueuedCount = 0;
			for (const TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
			{
				QueuedCount += Entry.Value.Phase == EVoxelLandChaosPhase::CookQueued
									|| Entry.Value.Phase == EVoxelLandChaosPhase::Apply
								 ? 1
								 : 0;
			}
			if (QueuedCount + MeshActive < Settings.MaxQueuedMeshes)
			{
				SubmitMesh(Key, Tile, MoveTemp(Field));
			}
		}
	}
}

void FVoxelLandChaosWorld::Tick()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosTick);
	const double Deadline = FPlatformTime::Seconds() + Settings.GameThreadBudgetMs / 1000.0;
	DrainCompletions();
	int32 PhysicsUpdates = 0;
	AdvanceRetirements(Deadline, PhysicsUpdates);
	if (bEnabled)
	{
		RefreshDemand();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosManageProps);
			Props.Tick(World);
		}
		const double Now = FPlatformTime::Seconds();
		TArray<FVoxelLandChunkKey> Order;
		Order.Reserve(Tiles.Num());
		for (TMap<FVoxelLandChunkKey, FVoxelLandChaosTile>::TIterator It = Tiles.CreateIterator(); It; ++It)
		{
			if (It.Value().Owners == 0 && It.Value().RetirementTime <= Now)
			{
				QueueRetirement(It.Value());
				It.RemoveCurrent();
			}
			else if (It.Value().Owners > 0)
			{
				Order.Add(It.Key());
			}
		}
		for (const FVoxelLandChunkKey& Key : Order)
		{
			FVoxelLandChaosTile& Tile = Tiles.FindChecked(Key);
			AdvanceTile(Key, Tile, Deadline, PhysicsUpdates);
		}
	}
	AdvanceRetirements(Deadline, PhysicsUpdates);
	if (!bEnabled && RetiredBodies.IsEmpty() && BodyOwner.IsValid())
	{
		if (AActor* Host = BodyOwner.Get())
		{
			Host->Destroy();
		}
		BodyOwner.Reset();
	}
	TRACE_COUNTER_SET(VoxelChaosMeshWorkers, MeshActive);
	TRACE_COUNTER_SET(VoxelChaosCookWorkers, CookActive);
	TRACE_COUNTER_SET(VoxelChaosActiveBodies, GetStats().ActiveBodies);
}

FVoxelLandChaosStats FVoxelLandChaosWorld::GetStats() const
{
	FVoxelLandChaosStats Stats;
	Stats.MeshActive = MeshActive;
	Stats.CookActive = CookActive;
	Stats.ResidentBytes = ResidentBytes;
	Stats.PendingBytes = GetAwaitingBytes() + CookBytes + GetCookedAwaitingInstallBytes();
	Stats.StaleResults = StaleResults;
	Stats.Failures = Failures;
	Stats.Holds = Props.GetHolds();
	Stats.Recoveries = Props.GetRecoveries();
	for (const TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		const FVoxelLandChaosTile& Tile = Entry.Value;
		Stats.ActiveBodies += Tile.Body.IsValid() ? 1 : 0;
		if (Tile.Owners > 0 && Tile.Phase == EVoxelLandChaosPhase::Field
			&& Tile.Detail != EVoxelLandChaosCollisionDetail::Disabled)
		{
			if (World.FindChunk(Entry.Key))
			{
				++Stats.MeshQueued;
			}
			else
			{
				++Stats.WaitingForField;
			}
		}
		Stats.CookQueued += Tile.Phase == EVoxelLandChaosPhase::CookQueued ? 1 : 0;
		Stats.ApplyQueued += Tile.Phase == EVoxelLandChaosPhase::Apply ? 1 : 0;
	}
	Stats.DetachedCooks = CookActive;
	for (const TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		Stats.DetachedCooks -= Entry.Value.Phase == EVoxelLandChaosPhase::Cooking ? 1 : 0;
	}
	return Stats;
}
