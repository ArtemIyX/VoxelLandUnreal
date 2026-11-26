#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Clipmap/Planning/VoxelLandClipmapLayout.h"
#include "Data/World/Streaming/VoxelLandWorldCancel.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AVoxelLandRegion;
class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
struct FVoxelLandM7SeamMesh;
struct FVoxelLandMeshData;
struct FVoxelLandMaterialAppearance;
struct FVoxelLandMeshingScratch;

namespace VoxelLandWorldPrivate
{
using FM7MeshPtr = TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>;
using FM7SourcePtr = TSharedPtr<const FVoxelLandM7SeamMesh, ESPMode::ThreadSafe>;
using FM7Sources = TMap<FVoxelLandChunkKey, FM7SourcePtr>;
using FM7Regions = TMap<FVoxelLandChunkKey, TWeakObjectPtr<AVoxelLandRegion>>;

struct FM7Draw
{
	FVoxelLandChunkKey Key;
	FVector LogicalOrigin = FVector::ZeroVector;
	FM7MeshPtr Terrain;
	FM7MeshPtr Curtain;
};

struct FM7RenderRevision
{
	FVoxelLandM7ClipmapLayout Layout;
	FM7Sources Sources;
	TArray<FM7Draw> Draws;
	TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance;
	int32 RefinedLeaves = 0;
};

struct FM7RenderRequest
{
	FVoxelLandM7ClipmapSettings Settings;
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe> Previous;
	TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance;
};

// Game thread only. Workers receive immutable request/previous revision pointers.
struct FM7VisualState
{
	FVoxelLandM7ClipmapSettings Settings;
	TOptional<FVector> DesiredView;
	TOptional<FVector> LastSubmittedView;
	TSharedPtr<FCancel, ESPMode::ThreadSafe> Cancel;
	uint64 Ticket = 0;
	uint64 PublishedTicket = 0;
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe> Published;
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe> Staged;
	uint64 StagedTicket = 0;
	int32 NextDraw = 0;
	FM7Regions Regions;
	FM7Regions StagedRegions;
};

FVoxelLandError BuildM7RenderRevision(const FM7RenderRequest& Request, const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker, FVoxelLandMeshingScratch& Scratch, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe>& OutRevision);
}
