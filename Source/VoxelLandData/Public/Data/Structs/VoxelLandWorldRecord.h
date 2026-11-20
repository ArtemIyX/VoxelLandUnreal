#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandWorldCancel.h"
#include "Data/Structs/VoxelLandChunkData.h"

namespace VoxelLandWorldPrivate
{
struct FRecord
{
	uint64 Ticket = 0;
	bool bExplicit = false, bDemand = false, bQueued = false, bRunning = false, bFailed = false;
	int64 MeshBytes = 0;
	int32 Vertices = 0, Triangles = 0;
	TSharedPtr<FCancel, ESPMode::ThreadSafe> Cancel;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data;
};
}
