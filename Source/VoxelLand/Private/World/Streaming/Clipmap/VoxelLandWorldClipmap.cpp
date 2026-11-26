#include "World/VoxelLandWorld.h"

#include "VoxelLandClipmapRenderWork.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/World/Streaming/VoxelLandPublication.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if !UE_SERVER
#include "Rendering/Components/VoxelLandChunkComponent.h"
#endif

using namespace VoxelLandWorldPrivate;

namespace
{
FVector QuantizeView(const FVector& View, double BaseSpacing)
{
	const double Step = 8.0 * BaseSpacing;
	return {FMath::RoundToDouble(View.X / Step) * Step, FMath::RoundToDouble(View.Y / Step) * Step,
		FMath::RoundToDouble(View.Z / Step) * Step};
}

bool OutsideReplacementWindow(
	const FVector& OldView, const FVector& NewView, const FVoxelLandM7ClipmapSettings& Settings)
{
	const FVector Delta = (NewView - OldView).GetAbs();
	const double MinDistance = double(Settings.BaseVoxelSizeCm) * 32.0;
	const double Horizontal = FMath::Max(MinDistance, Settings.HorizontalRadiusCm * 0.25);
	const double Vertical = FMath::Max(MinDistance, Settings.VerticalRadiusCm * 0.25);
	return Delta.X > Horizontal || Delta.Y > Horizontal || Delta.Z > Vertical;
}

bool AllRegionsValid(const FM7Regions& Regions)
{
	for (const auto& Pair : Regions)
	{
		if (!Pair.Value.IsValid())
		{
			return false;
		}
	}
	return true;
}

FVoxelLandChunkKey DrawRegionKey(const FM7Draw& Draw, const FIntVector& RegionDimensions)
{
	return {VoxelLandCoordinates::RegionOf(Draw.Key.Coordinate, RegionDimensions), Draw.Key.LOD};
}

bool SameHeightDraw(const FM7Draw& Draw, const FM7RenderRevision& Other)
{
	const int32* Index = Other.HeightDrawIndices.Find(Draw.Key);
	return Index && Other.Draws[*Index].Terrain == Draw.Terrain;
}

void FindReusableHeightRegions(FM7VisualState& Visual, const FIntVector& RegionDimensions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_FindReusableHeightClipmapRegions);
	Visual.ReusableStagedRegions.Reset();
	if (!Visual.Staged->bHeightSurface || !Visual.Published || !Visual.Published->bHeightSurface)
	{
		return;
	}
	for (const TPair<FVoxelLandChunkKey, TWeakObjectPtr<AVoxelLandRegion>>& Pair : Visual.Regions)
	{
		if (Pair.Value.IsValid())
		{
			Visual.ReusableStagedRegions.Add(Pair.Key);
		}
	}
	for (const FM7Draw& Draw : Visual.Published->Draws)
	{
		if (!SameHeightDraw(Draw, *Visual.Staged))
		{
			Visual.ReusableStagedRegions.Remove(DrawRegionKey(Draw, RegionDimensions));
		}
	}
	for (const FM7Draw& Draw : Visual.Staged->Draws)
	{
		if (!SameHeightDraw(Draw, *Visual.Published))
		{
			Visual.ReusableStagedRegions.Remove(DrawRegionKey(Draw, RegionDimensions));
		}
	}
}

int64 EstimateStagedBytes(const FM7MeshPtr& Mesh)
{
	return Mesh ? int64(Mesh->Vertices.GetAllocatedSize()) + Mesh->Indices.GetAllocatedSize()
			+ Mesh->MaterialBlend.GetAllocatedSize() + Mesh->AmbientVisibility.GetAllocatedSize()
		: 0;
}

bool SameSourceFrontier(const FM7RenderRevision& A, const FM7RenderRevision& B)
{
	if (A.bHeightSurface || B.bHeightSurface)
	{
		if (A.bHeightSurface != B.bHeightSurface || A.Draws.Num() != B.Draws.Num())
		{
			return false;
		}
		for (const FM7Draw& Draw : A.Draws)
		{
			if (!SameHeightDraw(Draw, B))
			{
				return false;
			}
		}
		return true;
	}
	if (A.Sources.Num() != B.Sources.Num())
	{
		return false;
	}
	for (const auto& Pair : A.Sources)
	{
		if (B.Sources.FindRef(Pair.Key) != Pair.Value)
		{
			return false;
		}
	}
	// With an immutable generator, mixed faces and their shared field frames
	// follow the same full leaf keys regardless of camera-priority ordering.
	return true;
}
}

void AVoxelLandWorld::UpdateM7Demand(const FVector& LogicalView)
{
	FM7VisualState& Visual = *State->M7Visual;
	FVector Quantized = QuantizeView(LogicalView, Visual.Settings.BaseVoxelSizeCm);
	if (Visual.bHeightSurface)
	{
		Quantized.Z = 0.0;
	}
	if (Quantized.ContainsNaN())
	{
		return;
	}
	if (Visual.LastSubmittedView
		&& OutsideReplacementWindow(Visual.LastSubmittedView.GetValue(), Quantized, Visual.Settings))
	{
		if (Visual.Cancel)
		{
			Visual.Cancel->bCancelled.Store(true);
		}
		if (Visual.Staged)
		{
			const uint64 Epoch = ConfigurationEpoch;
			DiscardM7Staging();
			if (!State || ConfigurationEpoch != Epoch)
			{
				return;
			}
		}
	}
	if (!Visual.LastSubmittedView || Quantized != Visual.LastSubmittedView.GetValue()
		|| (Visual.Cancel && Visual.Cancel->bCancelled.Load()))
	{
		Visual.DesiredView = Quantized;
	}
	else
	{
		Visual.DesiredView.Reset();
	}
}

void AVoxelLandWorld::DispatchM7Revision()
{
	if (!State || !State->M7Visual)
	{
		return;
	}
	FM7VisualState& Visual = *State->M7Visual;
	if (!Visual.DesiredView || Visual.Cancel || Visual.Staged)
	{
		return;
	}
	auto Request = MakeShared<FM7RenderRequest, ESPMode::ThreadSafe>();
	Request->Settings = Visual.Settings;
	Request->Settings.ViewCm = Visual.DesiredView.GetValue();
	Request->Previous = Visual.Published;
	Request->Appearance = State->Appearance;

	FJob Job;
	Job.Generator = State->Generator;
	Job.Epoch = State->Epoch;
	Job.Ticket = ++State->NextTicket;
	Job.Cancel = MakeShared<FCancel, ESPMode::ThreadSafe>();
	Job.M7Visual = Request;
#if WITH_DEV_AUTOMATION_TESTS
	Job.Barrier = MoveTemp(State->NextBarrier);
#endif
	Visual.Ticket = Job.Ticket;
	Visual.Cancel = Job.Cancel;
	Visual.LastSubmittedView = Request->Settings.ViewCm;
	Visual.DesiredView.Reset();
	{
		FScopeLock Lock(&State->Work->Mutex);
		State->Work->Jobs.Enqueue(MoveTemp(Job));
	}
	++State->Active;
	State->Work->Wake->Trigger();
}

void AVoxelLandWorld::AcceptM7Completion(const FCompletion& Done)
{
	FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	if (!State
		|| !VoxelLandPublication::IsCurrent(Visual != nullptr, Done.Job.Epoch, State->Epoch, Done.Job.Ticket,
			Visual ? Visual->Ticket : 0, Done.Job.Generator == State->Generator, Visual && Visual->Cancel.IsValid()))
	{
		if (State)
		{
			++State->Stale;
		}
		return;
	}
	const bool bCancelled = Visual->Cancel->bCancelled.Load();
	Visual->Cancel.Reset();
	if (Done.bCancelled || bCancelled)
	{
		++State->Cancelled;
		return;
	}
	if (Done.Error || !Done.M7Visual)
	{
		++State->Failed;
		const FString Error = Done.Error.Message.IsEmpty() ? TEXT("M7 clipmap returned no complete visual coverage")
														   : Done.Error.Message.Left(1024);
		if (M7ClipmapError != Error)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *GetName(), *Error);
			M7ClipmapError = Error;
		}
		return;
	}
	Visual->Staged = Done.M7Visual;
	Visual->StagedTicket = Done.Job.Ticket;
	Visual->NextDraw = 0;
	FindReusableHeightRegions(*Visual, State->RegionDimensions);
}

void AVoxelLandWorld::DiscardM7Staging()
{
	FM7VisualState& Visual = *State->M7Visual;
	FM7Regions Retired = MoveTemp(Visual.StagedRegions);
	Visual.Staged.Reset();
	Visual.StagedTicket = 0;
	Visual.NextDraw = 0;
	Visual.ReusableStagedRegions.Reset();
	for (const TPair<FVoxelLandChunkKey, TWeakObjectPtr<AVoxelLandRegion>>& Pair : Retired)
	{
		Visual.RetiredRegions.Add(Pair.Value);
	}
}

void AVoxelLandWorld::AdvanceM7Publication()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_StageM7VisualRevision);
	FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	UWorld* World = GetWorld();
	if (!Visual || !World || World->bIsTearingDown)
	{
		return;
	}
	constexpr int32 RetirementsPerTick = 2;
	constexpr double StageSecondsPerTick = 0.001;
	const double Deadline = FPlatformTime::Seconds() + StageSecondsPerTick;
	for (int32 Index = 0; Index < RetirementsPerTick && !Visual->RetiredRegions.IsEmpty(); ++Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_RetireClipmapRegion);
		const uint64 RetirementEpoch = ConfigurationEpoch;
		const TWeakObjectPtr<AVoxelLandRegion> WeakRegion = Visual->RetiredRegions.Pop(EAllowShrinking::No);
		if (AVoxelLandRegion* Region = WeakRegion.Get())
		{
			Region->Destroy();
		}
		if (!State || ConfigurationEpoch != RetirementEpoch)
		{
			return;
		}
		if (FPlatformTime::Seconds() >= Deadline)
		{
			break;
		}
	}
	if (!Visual->Staged)
	{
		return;
	}
	const uint64 Epoch = ConfigurationEpoch;
	const uint64 Ticket = Visual->StagedTicket;
	const auto Revision = Visual->Staged;
	const auto IsCurrent = [&]() {
		return ConfigurationEpoch == Epoch && State && State->M7Visual.Get() == Visual && Visual->StagedTicket == Ticket
			&& Visual->Ticket == Ticket && Visual->Staged == Revision;
	};
	if (Visual->Published && Visual->Published->Appearance == Revision->Appearance
		&& SameSourceFrontier(*Visual->Published, *Revision) && AllRegionsValid(Visual->Regions))
	{
		Visual->Published = Revision;
		Visual->PublishedTicket = Ticket;
		Visual->Staged.Reset();
		Visual->StagedTicket = 0;
		Visual->ReusableStagedRegions.Reset();
		M7FallbackRefinements = Revision->RefinedLeaves;
		M7ClipmapError.Reset();
		return;
	}

	constexpr int32 DrawsPerTick = 4;
	constexpr int64 StagedBytesPerTick = 512 * 1024;
	int32 StagedDraws = 0;
	int64 StagedBytes = 0;
	while (Visual->NextDraw < Revision->Draws.Num() && StagedDraws < DrawsPerTick)
	{
		const FM7Draw& Draw = Revision->Draws[Visual->NextDraw];
		const FVoxelLandChunkKey RegionKey = DrawRegionKey(Draw, State->RegionDimensions);
		if (Visual->ReusableStagedRegions.Contains(RegionKey))
		{
			++Visual->NextDraw;
			continue;
		}
		const int64 DrawBytes = EstimateStagedBytes(Draw.Terrain) + EstimateStagedBytes(Draw.Curtain);
		if (StagedDraws > 0 && (StagedBytes + DrawBytes > StagedBytesPerTick || FPlatformTime::Seconds() >= Deadline))
		{
			break;
		}
		++StagedDraws;
		StagedBytes += DrawBytes;
		AVoxelLandRegion* Region = Visual->StagedRegions.FindRef(RegionKey).Get();
		if (!Region)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Region = World->SpawnActor<AVoxelLandRegion>(AVoxelLandRegion::StaticClass(), FTransform::Identity, Params);
			if (!IsCurrent())
			{
				if (IsValid(Region))
				{
					Region->Destroy();
				}
				return;
			}
			if (!IsValid(Region))
			{
				M7ClipmapError = TEXT("Unable to stage an M7 render region");
				++State->Failed;
				DiscardM7Staging();
				return;
			}
			if (Revision->bHeightSurface)
			{
				Region->BeginHeightClipmapStaging();
			}
			else
			{
				Region->SetActorHiddenInGame(true);
			}
			Visual->StagedRegions.Add(RegionKey, Region);
		}
		const bool bTerrainReady =
			!Draw.Terrain || Region->Publish(Draw.Key.Coordinate, Draw.LogicalOrigin, Draw.Terrain, TerrainMaterial);
		if (!IsCurrent())
		{
			return;
		}
		const bool bCurtainReady =
			!Draw.Curtain
			|| Region->Publish(Draw.Key.Coordinate, Draw.LogicalOrigin, Draw.Curtain, TerrainMaterial, true);
		if (!IsCurrent())
		{
			return;
		}
		if (!bTerrainReady || !bCurtainReady)
		{
			M7ClipmapError = TEXT("An M7 terrain or curtain component rejected the staged mesh");
			++State->Failed;
			DiscardM7Staging();
			return;
		}
		++Visual->NextDraw;
	}
	if (Visual->NextDraw != Revision->Draws.Num())
	{
		return;
	}
	bool bNeedsRestaging = false;
	for (TSet<FVoxelLandChunkKey>::TIterator It = Visual->ReusableStagedRegions.CreateIterator(); It; ++It)
	{
		if (!Visual->Regions.FindRef(*It).IsValid())
		{
			It.RemoveCurrent();
			bNeedsRestaging = true;
		}
	}
	if (bNeedsRestaging)
	{
		Visual->NextDraw = 0;
		return;
	}
	if (!AllRegionsValid(Visual->StagedRegions))
	{
		M7ClipmapError = TEXT("An M7 staged render region was retired before publication");
		++State->Failed;
		DiscardM7Staging();
		return;
	}

	// The full replacement has validated components before any old region is
	// hidden or retired. Both terrain and skirt have separate render payloads.
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_SwapClipmapRegions);
	FM7Regions Old = MoveTemp(Visual->Regions);
	Visual->Regions = MoveTemp(Visual->StagedRegions);
	for (const FVoxelLandChunkKey& Key : Visual->ReusableStagedRegions)
	{
		TWeakObjectPtr<AVoxelLandRegion> Region;
		if (Old.RemoveAndCopyValue(Key, Region))
		{
			Visual->Regions.Add(Key, Region);
		}
	}
	Visual->Published = Revision;
	Visual->PublishedTicket = Ticket;
	Visual->Staged.Reset();
	Visual->StagedTicket = 0;
	Visual->NextDraw = 0;
	M7PublishedLeafCount = Revision->Layout.Leaves.Num();
	M7FallbackRefinements = Revision->RefinedLeaves;
	M7ClipmapError.Reset();
	TArray<TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe>> Show;
	TArray<TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe>> Hide;
	for (const TPair<FVoxelLandChunkKey, TWeakObjectPtr<AVoxelLandRegion>>& Pair : Visual->Regions)
	{
		if (!Visual->ReusableStagedRegions.Contains(Pair.Key))
		{
			AVoxelLandRegion* Region = Pair.Value.Get();
			Regions.Add(Region);
			Region->SetTerrainMaterial(TerrainMaterial);
			const bool bVisible = !State->HiddenRegions.Contains(Pair.Key.Coordinate);
			if (TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> Visibility = Region->SetHeightClipmapVisible(bVisible))
			{
				if (bVisible)
				{
					Show.Add(MoveTemp(Visibility));
				}
			}
			else
			{
				Region->SetActorHiddenInGame(!bVisible);
			}
		}
	}
	Visual->ReusableStagedRegions.Reset();
	for (const TPair<FVoxelLandChunkKey, TWeakObjectPtr<AVoxelLandRegion>>& Pair : Old)
	{
		if (AVoxelLandRegion* Region = Pair.Value.Get())
		{
			Regions.Remove(Region);
			if (TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> Visibility = Region->SetHeightClipmapVisible(false))
			{
				Hide.Add(MoveTemp(Visibility));
			}
			else
			{
				Region->SetActorHiddenInGame(true);
			}
			Visual->RetiredRegions.Add(Pair.Value);
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 AVoxelLandWorld::GetM7PublishedTicketForTests() const
{
	return State && State->M7Visual ? State->M7Visual->PublishedTicket : 0;
}

bool AVoxelLandWorld::IsM7ReplacementPendingForTests() const
{
	const FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	return Visual && (Visual->DesiredView.IsSet() || Visual->Cancel.IsValid() || Visual->Staged.IsValid());
}

void AVoxelLandWorld::CopyM7PublishedKeysForTests(TArray<FVoxelLandChunkKey>& Out) const
{
	const FM7VisualState* Visual = State ? State->M7Visual.Get() : nullptr;
	Out.Reset();
	if (Visual && Visual->Published)
	{
		Out = Visual->Published->Layout.Leaves;
	}
#if !UE_SERVER
	UVoxelLandChunkComponent::CommitClipmapVisibility(MoveTemp(Show), MoveTemp(Hide));
#endif
}
#endif
