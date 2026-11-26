#pragma once

#include "CoreMinimal.h"
#include "VoxelLandDensityBounds.generated.h"

USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandDensityBounds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Bounds")
	FIntVector MinChunk = FIntVector(-8, -8, -4);

	UPROPERTY(EditAnywhere, Category = "Bounds")
	FIntVector MaxChunkExclusive = FIntVector(8, 8, 4);

	bool IsValid() const
	{
		return MinChunk.X < MaxChunkExclusive.X && MinChunk.Y < MaxChunkExclusive.Y && MinChunk.Z < MaxChunkExclusive.Z;
	}

	bool Contains(const FIntVector& Chunk) const
	{
		return Chunk.X >= MinChunk.X && Chunk.X < MaxChunkExclusive.X && Chunk.Y >= MinChunk.Y
			&& Chunk.Y < MaxChunkExclusive.Y && Chunk.Z >= MinChunk.Z && Chunk.Z < MaxChunkExclusive.Z;
	}

	bool operator==(const FVoxelLandDensityBounds& Other) const
	{
		return MinChunk == Other.MinChunk && MaxChunkExclusive == Other.MaxChunkExclusive;
	}
};
