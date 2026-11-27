#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"

class AVoxelLandPCGCell;

struct FVoxelLandPCGCellRecord
{
	FBox Ownership = FBox(ForceInit);
	FGuid Lease;
	TArray<FVoxelLandChunkKey> Dependencies;
	TWeakObjectPtr<AVoxelLandPCGCell> Pending;
	TWeakObjectPtr<AVoxelLandPCGCell> Visible;
	FIntVector CaptureOrigin = FIntVector::ZeroValue;
	bool bComplete = false;
};
