#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandWorldJob.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"

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
