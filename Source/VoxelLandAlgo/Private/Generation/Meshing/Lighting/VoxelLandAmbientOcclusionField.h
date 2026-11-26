#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "../../Clipmap/Sampling/VoxelLandLocalTerrainHeightSource.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
struct FVoxelLandChunkData;
struct FVoxelLandM7CompletedBox;

class FVoxelLandAmbientOcclusionField final
{
public:
	FVoxelLandAmbientOcclusionField(const FVoxelLandGenerator& InGenerator, FVoxelLandWorkerContext& InWorker,
		TFunctionRef<bool()> InIsCancelled, const FVoxelLandChunkData* InLOD0Data);

	static bool Supports(const FVoxelLandGenerator& Generator, uint8 Lod);
	[[nodiscard]] FVoxelLandError operator()(const FInt64Vector& Position, int16& OutDensity);

private:
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;

	[[nodiscard]] FVoxelLandError ReadHeight(const FInt64Vector& Position, int16& OutDensity);
	[[nodiscard]] FVoxelLandError ReadHeightEdge(const FInt64Vector& Position, int16& OutDensity);
	[[nodiscard]] FVoxelLandError ReadHeightBox(const FVoxelLandM7CompletedBox& Box, TArray<int16>& Out);
	[[nodiscard]] FVoxelLandError ReadDirect(const FInt64Vector& Position, int16& OutDensity);
	static bool TryReadChunk(const FVoxelLandChunkData& Chunk, const FInt64Vector& Position, int16& OutDensity);

	const FVoxelLandGenerator& Generator;
	FVoxelLandWorkerContext& Worker;
	TFunctionRef<bool()> IsCancelled;
	const FVoxelLandChunkData* const LOD0Data;
	const bool bHasHeight;
	FVoxelLandM7LocalHeightSource LocalHeightSource;
	int64 PlannedHeightWork = 0;
	TMap<FInt64Vector, TArray<int16>> HeightTiles;
	TMap<FInt64Vector, int16> HeightEdgeSamples;
	TMap<FIntVector, FChunkPtr> DirectChunks;
};
