#pragma once

#include "CoreMinimal.h"
#include "Data/World/Streaming/VoxelLandWorldJob.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"

namespace VoxelLandWorldPrivate
{
struct FM7RenderRevision;

struct FCompletion
{
	FJob Job;
	FVoxelLandError Error;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data;
	TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh;
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe> M7Visual;
	bool bCancelled = false;
};
}
