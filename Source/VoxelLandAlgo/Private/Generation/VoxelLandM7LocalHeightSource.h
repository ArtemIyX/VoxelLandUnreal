#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

struct FVoxelLandM4UnlimitedConfig;

class FVoxelLandM7LocalHeightSource final
{
public:
	explicit FVoxelLandM7LocalHeightSource(const FVoxelLandM4UnlimitedConfig* InConfig);
	bool operator()(int64 X, int64 Y, int32& Height);
	FVoxelLandError PreferSourceError(const FVoxelLandError& Fallback) const;

private:
	static constexpr int32 MaxCacheEntries = 8192;
	static constexpr int64 MaxControlWork = 64 * 1024 * 1024;

	const FVoxelLandM4UnlimitedConfig* Config;
	TMap<FInt64Vector, int32> Heights;
	TArray<int64> Registers[3];
	int64 CostPerHeight = 0;
	int64 RemainingWork = MaxControlWork;
	FVoxelLandError Error;
};
