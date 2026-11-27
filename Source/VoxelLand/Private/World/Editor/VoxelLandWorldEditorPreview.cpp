#include "World/VoxelLandWorld.h"

#if WITH_EDITOR

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "World/VoxelLandWorldCoordinates.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool AVoxelLandWorld::ShouldTickIfViewportsOnly() const
{
	return bIsEditorTerrainPreview && GetWorld() && GetWorld()->WorldType == EWorldType::Editor;
}

bool AVoxelLandWorld::InitializeFiniteEditorPreview(UVoxelLandWorldConfig* Config)
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::Editor || World->bIsTearingDown || !HasAnyFlags(RF_Transient))
	{
		LastInitializationError = TEXT("Terrain preview needs a transient world actor in the level editor.");
		return false;
	}
	if (!IsValid(Config) || Config->IsInfinite())
	{
		LastInitializationError = TEXT("Select a finite VoxelLand World Config for the terrain preview.");
		return false;
	}
	bIsEditorTerrainPreview = true;
	bEnableM7Clipmap = false;
	return InitializeFromConfig(Config, FIntVector::ZeroValue);
}

bool AVoxelLandWorld::RequestEditorPreviewBounds(const FBox& Bounds, FBox& OutClippedBounds, FString& OutError)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_EditorPreview_Admit);
	OutError.Reset();
	OutClippedBounds = FBox(ForceInit);
	if (!State || !ShouldTickIfViewportsOnly() || bHasEditorTerrainPreviewDemand || GetWorld()->bIsTearingDown)
	{
		OutError = TEXT("Terrain preview demand needs a freshly initialized preview world.");
		return false;
	}

	const FIntVector Origin = GetWorld()->OriginLocation;
	FVector Min;
	FVector Max;
	if (!Bounds.IsValid || !VoxelLandWorldCoordinates::ToGlobal(Bounds.Min, Origin, Min)
		|| !VoxelLandWorldCoordinates::ToGlobal(Bounds.Max, Origin, Max))
	{
		OutError = TEXT("The preview bounds are invalid.");
		return false;
	}
	const FVoxelLandGenerator& Generator = *State->Generator;
	const double Width = double(Generator.GetProfile().VoxelSizeCm) * FVoxelLandChunkData::Cells;
	if (const TOptional<FVoxelLandDensityBounds>& DensityBounds = Generator.GetDirectDensityBounds())
	{
		Min = Min.ComponentMax(FVector(DensityBounds->MinChunk) * Width);
		Max = Max.ComponentMin(FVector(DensityBounds->MaxChunkExclusive) * Width);
	}
	else if (const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid = Generator.GetM4HeightGrid())
	{
		int64 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
		Grid->GetBoundsCm(MinX, MinY, MaxX, MaxY);
		Min.X = FMath::Max(Min.X, double(MinX));
		Min.Y = FMath::Max(Min.Y, double(MinY));
		Max.X = FMath::Min(Max.X, double(MaxX));
		Max.Y = FMath::Min(Max.Y, double(MaxY));
	}

	constexpr int32 MaxPreviewChunks = 256;
	int32 Count = 1;
	FIntVector First;
	FIntVector Last;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Min[Axis] >= Max[Axis])
		{
			bHasEditorTerrainPreviewDemand = true;
			return true;
		}
		const double Lower = FMath::FloorToDouble(Min[Axis] / Width);
		const double Upper = FMath::CeilToDouble(Max[Axis] / Width) - 1.0;
		const double Length = Upper - Lower + 1.0;
		if (Lower < MIN_int32 || Upper >= MAX_int32 || Length < 1.0 || Length > MaxPreviewChunks / Count)
		{
			OutError = FString::Printf(
				TEXT("The clipped preview exceeds its coordinate or %d chunk budget."), MaxPreviewChunks);
			return false;
		}
		First[Axis] = static_cast<int32>(Lower);
		Last[Axis] = static_cast<int32>(Upper);
		Count *= static_cast<int32>(Length);
	}

	TArray<FVoxelLandChunkKey> Keys;
	Keys.Reserve(Count);
	FIntVector FirstAdmitted(MAX_int32);
	FIntVector LastAdmitted(MIN_int32);
	const FIntVector Size = Last - First + FIntVector(1);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVoxelLandChunkKey Key;
		Key.Coordinate = First + FIntVector(Index % Size.X, Index / Size.X % Size.Y, Index / (Size.X * Size.Y));
		if (!CanRequestChunk(Key))
		{
			continue;
		}
		Keys.Add(Key);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FirstAdmitted[Axis] = FMath::Min(FirstAdmitted[Axis], Key.Coordinate[Axis]);
			LastAdmitted[Axis] = FMath::Max(LastAdmitted[Axis], Key.Coordinate[Axis]);
		}
	}
	if (Keys.Num() > MaxResidentChunks)
	{
		OutError = TEXT("The preview exceeds the terrain resident chunk budget.");
		return false;
	}
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		if (!RequestChunk(Key))
		{
			OutError = TEXT("The terrain scheduler could not reserve the preview chunks.");
			return false;
		}
	}
	EditorTerrainPreviewKeys = MoveTemp(Keys);
	bHasEditorTerrainPreviewDemand = true;
	if (!EditorTerrainPreviewKeys.IsEmpty())
	{
		Min = Min.ComponentMax(FVector(FirstAdmitted) * Width);
		Max = Max.ComponentMin(FVector(LastAdmitted + FIntVector(1)) * Width);
		OutClippedBounds = FBox(Min - FVector(Origin), Max - FVector(Origin));
	}
	return true;
}

bool AVoxelLandWorld::PollEditorTerrainPreview(FString& OutError) const
{
	check(IsInGameThread());
	OutError.Reset();
	if (!State || !bHasEditorTerrainPreviewDemand)
	{
		return false;
	}
	bool bHasPendingChunk = false;
	for (const FVoxelLandChunkKey& Key : EditorTerrainPreviewKeys)
	{
		const VoxelLandWorldPrivate::FRecord* Record = State->Records.Find(Key);
		if (!Record || Record->bFailed)
		{
			OutError = Record && !Record->Error.IsEmpty() ? Record->Error : TEXT("A terrain preview chunk failed.");
			return true;
		}
		bHasPendingChunk |= !Record->Data || Record->bQueued || Record->bRunning || Record->bNeedsRender;
	}
	return !bHasPendingChunk;
}

void AVoxelLandWorld::SetEditorTerrainPreviewVisible(bool bVisible)
{
	check(IsInGameThread());
	if (!bIsEditorTerrainPreview || bIsEditorTerrainPreviewVisible == bVisible)
	{
		return;
	}
	bIsEditorTerrainPreviewVisible = bVisible;
	for (AVoxelLandRegion* Region : Regions)
	{
		if (IsValid(Region))
		{
			Region->SetIsTemporarilyHiddenInEditor(!bVisible);
		}
	}
}

#endif
