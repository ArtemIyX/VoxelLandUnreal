#include "VoxelLandPCGComponent.h"

#include "VoxelLandPCGCell.h"
#include "VoxelLandPCGTerrainData.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TRACE_DECLARE_INT_COUNTER(VoxelLandPCGCellCount, TEXT("VoxelLand/PCG/Cells"));
TRACE_DECLARE_INT_COUNTER(VoxelLandPCGJobCount, TEXT("VoxelLand/PCG/Jobs"));
TRACE_DECLARE_MEMORY_COUNTER(VoxelLandPCGPendingField, TEXT("VoxelLand/PCG/PendingFieldBytes"));

struct FVoxelLandPCGFieldReservation
{
	static constexpr int64 BytesPerChunk = FVoxelLandChunkData::SampleCount * sizeof(int16);
	static constexpr int64 BytesPerMiB = 1024 * 1024;
};
#include "World/VoxelLandWorld.h"

bool UVoxelLandPCGComponent::ValidateLayers()
{
	if (Layers.IsEmpty())
	{
		PlacementStatus = TEXT("Assign a PCG layer and mesh");
		return false;
	}
	TSet<FName> IDs;
	for (const FVoxelLandPCGLayer& Layer : Layers)
	{
		if (Layer.LayerId.IsNone() || IDs.Contains(Layer.LayerId) || !Layer.Graph || !Layer.Graph->GetGraph()
			|| static_cast<bool>(Layer.StaticMesh) == static_cast<bool>(Layer.SkeletalMesh))
		{
			PlacementStatus = TEXT("Each PCG layer requires a unique name, a graph, and one mesh");
			return false;
		}
		for (const UPCGNode* Node : Layer.Graph->GetGraph()->GetNodes())
		{
			const UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
			if (!Settings)
			{
				continue;
			}
			const FString Name = Settings->GetClass()->GetName();
			if (Name.Contains(TEXT("Spawner")) || Name.Contains(TEXT("SpawnActor")) || Name.Contains(TEXT("Subgraph"))
				|| Name.Contains(TEXT("Blueprint")))
			{
				PlacementStatus = TEXT("Connect the graph to a point output. The VoxelLand layer publishes its mesh.");
				return false;
			}
		}
		IDs.Add(Layer.LayerId);
	}
	return true;
}

void UVoxelLandPCGComponent::BuildDesiredCells(const double CellSizeCm)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_SelectCells);
	const FVector Offset(GetWorld()->OriginLocation);
#if WITH_EDITOR
	if (bPreview && bShowPreviewCandidatePoints)
	{
		int32 DebugBudget = 512;
		for (const TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
		{
			if (const AVoxelLandPCGCell* Actor = Entry.Value.Visible.Get())
			{
				DebugBudget -= Actor->DrawDebugPoints(DebugBudget);
				if (DebugBudget <= 0)
				{
					break;
				}
			}
		}
	}
#endif
	TMap<FVoxelLandPCGCellKey, FBox> Desired;
	CellOrder.Reset();
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		if (bPreview)
		{
			AddRegion(Index, PreviewLogicalBounds, PreviewLogicalBounds.GetCenter(), CellSizeCm, true, Desired);
			continue;
		}
		const FVoxelLandPCGLayer& Layer = Layers[Index];
		for (FConstPlayerControllerIterator Player = GetWorld()->GetPlayerControllerIterator(); Player; ++Player)
		{
			const APlayerController* Controller = Player->Get();
			if (!Controller || !Controller->IsLocalController())
			{
				continue;
			}
			FVector View;
			FRotator Rotation;
			Controller->GetPlayerViewPoint(View, Rotation);
			View += Offset;
			const double Radius = FMath::Clamp(Layer.GenerationRadiusCm + Layer.RetirementMarginCm, 0.0, 1000000.0);
			const UVoxelLandWorldConfig* Config = ResolveWorldConfig();
			const double VerticalPadding = (FMath::Clamp(VerticalCells, 0, 4) + 0.5) * CellSizeCm;
			FVector Origin = View;
			double Height = VerticalPadding;
			if (Layer.bOverrideVerticalBounds && Layer.LogicalHeightRangeCm.X < Layer.LogicalHeightRangeCm.Y)
			{
				Origin.Z = (Layer.LogicalHeightRangeCm.X + Layer.LogicalHeightRangeCm.Y) * 0.5;
				Height = (Layer.LogicalHeightRangeCm.Y - Layer.LogicalHeightRangeCm.X) * 0.5;
			}
			else if (Config && !Config->IsDirectDensity())
			{
				Origin.Z = Config->BaseElevationCm;
				Height = FMath::Max(VerticalPadding, static_cast<double>(FMath::Abs(Config->HeightScaleCm)));
			}
			const FVector Extents(Radius, Radius, Height);
			AddRegion(Index, FBox(Origin - Extents, Origin + Extents), Origin, CellSizeCm, false, Desired);
		}
	}
	int32 RemainingRetirements = 4;
	for (TMap<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>::TIterator It = Cells.CreateIterator(); It; ++It)
	{
		if (RemainingRetirements == 0 || Desired.Contains(It.Key()))
		{
			continue;
		}
		CancelCell(It.Value());
		if (AVoxelLandPCGCell* Visible = It.Value().Visible.Get())
		{
			Visible->Destroy();
		}
		It.RemoveCurrent();
		--RemainingRetirements;
	}
	for (const FVoxelLandPCGCellKey& Key : CellOrder)
	{
		if (!Cells.Contains(Key) && Cells.Num() >= FMath::Clamp(MaxResidentCells, 1, 512))
		{
			continue;
		}
		Cells.FindOrAdd(Key).Ownership = Desired.FindChecked(Key);
	}
}

void UVoxelLandPCGComponent::AddRegion(const int32 Layer, const FBox& Bounds, const FVector& Focus,
	const double CellSizeCm, const bool bClip, TMap<FVoxelLandPCGCellKey, FBox>& Desired)
{
	const FVector MinIndex = Bounds.Min / CellSizeCm;
	const FVector MaxIndex = Bounds.Max / CellSizeCm;
	constexpr double MaxCellIndex = 10000000.0;
	if (Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN() || MinIndex.GetAbsMax() > MaxCellIndex
		|| MaxIndex.GetAbsMax() > MaxCellIndex)
	{
		return;
	}
	const FIntVector First(FMath::FloorToInt(MinIndex.X), FMath::FloorToInt(MinIndex.Y), FMath::FloorToInt(MinIndex.Z));
	const FIntVector Last(
		FMath::CeilToInt(MaxIndex.X) - 1, FMath::CeilToInt(MaxIndex.Y) - 1, FMath::CeilToInt(MaxIndex.Z) - 1);
	const FIntVector Center(FMath::FloorToInt(Focus.X / CellSizeCm), FMath::FloorToInt(Focus.Y / CellSizeCm),
		FMath::FloorToInt(Focus.Z / CellSizeCm));
	const int32 MaxRing = FMath::Min(256, FMath::Max((Last - Center).GetAbsMax(), (First - Center).GetAbsMax()));
	const int32 Limit = FMath::Clamp(MaxResidentCells, 1, 512) / FMath::Max(1, Layers.Num());
	const int32 StartCount = Desired.Num();
	for (int32 Ring = 0; Ring <= MaxRing && Desired.Num() - StartCount < Limit; ++Ring)
	{
		const int32 MinX = FMath::Max(First.X, Center.X - Ring);
		const int32 MaxX = FMath::Min(Last.X, Center.X + Ring);
		const int32 MinY = FMath::Max(First.Y, Center.Y - Ring);
		const int32 MaxY = FMath::Min(Last.Y, Center.Y + Ring);
		for (int32 Y = MinY; Y <= MaxY && Desired.Num() - StartCount < Limit; ++Y)
		{
			for (int32 X = MinX; X <= MaxX && Desired.Num() - StartCount < Limit; ++X)
			{
				if (FMath::Max(FMath::Abs(X - Center.X), FMath::Abs(Y - Center.Y)) != Ring)
				{
					continue;
				}
				for (int32 Z = First.Z; Z <= Last.Z && Desired.Num() - StartCount < Limit; ++Z)
				{
					const FVoxelLandPCGCellKey Key{FIntVector(X, Y, Z), Layer};
					const FVector Min = FVector(Key.Coordinate) * CellSizeCm;
					const FBox CellBounds(Min, Min + FVector(CellSizeCm));
					const double Distance = FVector::Dist2D(CellBounds.GetClosestPointTo(Focus), Focus);
					const FVoxelLandPCGLayer& Setting = Layers[Layer];
					const double Range =
						Setting.GenerationRadiusCm + (Cells.Contains(Key) ? Setting.RetirementMarginCm : 0.0);
					if (!Desired.Contains(Key) && (bClip || Distance <= Range))
					{
						Desired.Add(Key, bClip ? CellBounds.Overlap(Bounds) : CellBounds);
						CellOrder.Add(Key);
					}
				}
			}
		}
	}
}

int32 UVoxelLandPCGComponent::GetCellSeed(const FVoxelLandPCGCellKey& Key) const
{
	const FVoxelLandPCGLayer& Layer = Layers[Key.Layer];
	const UVoxelLandWorldConfig* Config = ResolveWorldConfig();
	uint32 Hash = GetTypeHash(Config ? Config->Seed : 0);
	Hash = HashCombineFast(Hash, FCrc::StrCrc32(*Layer.LayerId.ToString()));
	Hash = HashCombineFast(Hash, GetTypeHash(Layer.SeedOffset));
	return static_cast<int32>(HashCombineFast(Hash, GetTypeHash(Key.Coordinate)));
}

void UVoxelLandPCGComponent::ProcessCells()
{
	const double Deadline = FPlatformTime::Seconds() + 0.001;
	int32 Jobs = 0;
	int64 PendingFieldBytes = 0;
	for (const TPair<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord>& Entry : Cells)
	{
		if (Entry.Value.Lease.IsValid() || Entry.Value.Pending.IsValid())
		{
			PendingFieldBytes += Entry.Value.Dependencies.Num() * FVoxelLandPCGFieldReservation::BytesPerChunk;
			++Jobs;
		}
	}
	int32 Published = 0;
	for (const FVoxelLandPCGCellKey& Key : CellOrder)
	{
		FVoxelLandPCGCellRecord* Record = Cells.Find(Key);
		if (!Record || Record->bComplete || FPlatformTime::Seconds() >= Deadline)
		{
			continue;
		}
		if (Record->Lease.IsValid() || Record->Pending.IsValid())
		{
			if (Published < 2 && AdvanceCell(Key, *Record))
			{
				PendingFieldBytes -= Record->Dependencies.Num() * FVoxelLandPCGFieldReservation::BytesPerChunk;
				Record->Dependencies.Reset();
				++Published;
				--Jobs;
			}
			continue;
		}
		if (Jobs < FMath::Clamp(MaxCellJobs, 1, 12) && StartCell(Key, *Record, PendingFieldBytes))
		{
			++Jobs;
		}
	}
	TRACE_COUNTER_SET(VoxelLandPCGCellCount, Cells.Num());
	TRACE_COUNTER_SET(VoxelLandPCGJobCount, Jobs);
	TRACE_COUNTER_SET(VoxelLandPCGPendingField, PendingFieldBytes);
	if (PlacementStatus.IsEmpty() || PlacementStatus.StartsWith(TEXT("Waiting"))
		|| PlacementStatus.StartsWith(TEXT("Compiling")) || PlacementStatus.StartsWith(TEXT("Ready")))
	{
		PlacementStatus =
			FString::Printf(TEXT("Ready %d / %d cells, %d active jobs"), GetVisibleCellCount(), Cells.Num(), Jobs);
	}
}

bool UVoxelLandPCGComponent::StartCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record,
	int64& PendingFieldBytes)
{
	const FVector Offset(GetWorld()->OriginLocation);
	const double Margin = FMath::Clamp(Layers[Key.Layer].NeighborMarginCm, 0.0, 20000.0);
	const FBox Bounds = Record.Ownership.ExpandBy(FVector(Margin, Margin, 0.0)).ShiftBy(-Offset);
	if (!ResolveSourceWorld()->CollectTerrainPlacementKeys(Bounds, Record.Dependencies))
	{
		Record.bComplete = true;
		PlacementStatus = TEXT("PCG cell exceeds terrain lease capacity");
		return false;
	}
	if (Record.Dependencies.IsEmpty())
	{
		FinishCell(Key, Record);
		return false;
	}
	const int64 RequiredBytes = Record.Dependencies.Num() * FVoxelLandPCGFieldReservation::BytesPerChunk;
	const int64 MaxBytes = FMath::Clamp(MaxPendingFieldMiB, 1, 128) * FVoxelLandPCGFieldReservation::BytesPerMiB;
	if (RequiredBytes > MaxBytes)
	{
		Record.bComplete = true;
		PlacementStatus = TEXT("One PCG cell exceeds its pending field budget");
		return false;
	}
	if (PendingFieldBytes > MaxBytes - RequiredBytes)
	{
		PlacementStatus = TEXT("Waiting for pending field budget");
		return false;
	}
	const FGuid Id = FGuid::NewGuid();
	if (!ResolveSourceWorld()->UpdateCollisionDemand(Id, Record.Dependencies))
	{
		PlacementStatus = TEXT("Waiting for terrain lease capacity");
		return false;
	}
	Record.Lease = Id;
	PendingFieldBytes += RequiredBytes;
	return true;
}

bool UVoxelLandPCGComponent::AdvanceCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record)
{
	if (const AVoxelLandPCGCell* Pending = Record.Pending.Get())
	{
		if (Pending->IsGenerating())
		{
			return false;
		}
		FinishCell(Key, Record);
		return true;
	}
	UPCGVoxelTerrainData* Snapshot = nullptr;
	FString Error;
	const EVoxelLandPCGTerrainDataStatus Status = UPCGVoxelTerrainData::Capture(
		*ResolveSourceWorld(), Record.Lease, Record.Dependencies, Record.Ownership, this, Snapshot, &Error);
	if (Status == EVoxelLandPCGTerrainDataStatus::Pending)
	{
		return false;
	}
	ReleaseLease(Record);
	if (Status == EVoxelLandPCGTerrainDataStatus::Empty)
	{
		FinishCell(Key, Record);
		return true;
	}
	if (Status != EVoxelLandPCGTerrainDataStatus::Ready || !Snapshot)
	{
		PlacementStatus = Error.IsEmpty() ? TEXT("PCG terrain capture failed") : MoveTemp(Error);
		Record.bComplete = true;
		return true;
	}

	Record.CaptureOrigin = GetWorld()->OriginLocation;
	const UVoxelLandWorldConfig* Config = ResolveWorldConfig();
	Snapshot->SetMaskProgram(MaskProgram, static_cast<uint64>(Config ? Config->Seed : 0), MaskSignals);
#if WITH_EDITOR
	if (bPreview && bShowPreviewCandidatePoints)
	{
		Snapshot->EnableCandidateDebug();
	}
#endif
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVoxelLandPCGCell* Actor = GetWorld()->SpawnActor<AVoxelLandPCGCell>(
		Record.Ownership.GetCenter() - FVector(Record.CaptureOrigin), FRotator::ZeroRotator, Spawn);
	if (!Actor || !Actor->Start(Snapshot, Layers[Key.Layer].Graph, GetCellSeed(Key)))
	{
		if (Actor)
		{
			Actor->Destroy();
		}
		Record.bComplete = true;
		PlacementStatus = TEXT("PCG graph could not start");
		return true;
	}
	Record.Pending = Actor;
	return false;
}

void UVoxelLandPCGComponent::FinishCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_ApplyCell);
	Record.bComplete = true;
	AVoxelLandPCGCell* Pending = Record.Pending.Get();
	if (Pending)
	{
		TArray<FTransform> Output;
		if (!Pending->Collect(
				Output, Record.Ownership, Record.CaptureOrigin, FMath::Clamp(MaxInstancesPerCell, 1, 4096)))
		{
			PlacementStatus = TEXT("PCG point output failed or exceeded the cell limit");
			CancelCell(Record);
			return;
		}
#if WITH_EDITOR
		if (bPreview && bShowPreviewCandidatePoints)
		{
			Pending->RecordDebugPoints(Output);
		}
#endif
		Pending->StopExecution();
		Pending->Publish(Layers[Key.Layer], Output);
	}
	if (AVoxelLandPCGCell* Previous = Record.Visible.Get())
	{
		Previous->Destroy();
	}
	Record.Visible = Pending;
	Record.Pending.Reset();
}

void UVoxelLandPCGComponent::ReleaseLease(FVoxelLandPCGCellRecord& Record)
{
	if (Record.Lease.IsValid() && IsValid(ResolveSourceWorld()))
	{
		ResolveSourceWorld()->ReleaseCollisionDemand(Record.Lease);
	}
	Record.Lease.Invalidate();
}

void UVoxelLandPCGComponent::CancelCell(FVoxelLandPCGCellRecord& Record)
{
	ReleaseLease(Record);
	if (AVoxelLandPCGCell* Pending = Record.Pending.Get())
	{
		Pending->StopExecution();
		Pending->Destroy();
	}
	Record.Pending.Reset();
}
