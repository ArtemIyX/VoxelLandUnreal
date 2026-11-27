#pragma once

#include "CoreMinimal.h"

struct FVoxelLandPCGCellKey
{
	FIntVector Coordinate = FIntVector::ZeroValue;
	int32 Layer = 0;

	bool operator==(const FVoxelLandPCGCellKey&) const = default;

	friend uint32 GetTypeHash(const FVoxelLandPCGCellKey& Key)
	{
		return HashCombineFast(GetTypeHash(Key.Coordinate), GetTypeHash(Key.Layer));
	}
};
