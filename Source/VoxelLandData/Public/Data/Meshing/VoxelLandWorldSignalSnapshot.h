#pragma once

#include "Data/Meshing/VoxelLandWorldSignalGrid.h"

class VOXELLANDDATA_API FVoxelLandWorldSignalSnapshot final
{
public:
	using FPtr = TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe>;

	[[nodiscard]] static bool Create(
		TArray<FVoxelLandWorldSignalGrid> Grids, uint64 Revision, FPtr& Out, FString& Error);
	[[nodiscard]] bool Sample(FName Name, const FVector2d& LogicalXY, double& Out) const;
	bool HasChannel(FName Name) const { return Channels.Contains(Name); }
	uint64 GetRevision() const { return Revision; }

private:
	static bool Validate(const FVoxelLandWorldSignalGrid& Grid, int64& TotalSamples, FString& Error);
	TMap<FName, FVoxelLandWorldSignalGrid> Channels;
	uint64 Revision = 0;
};
