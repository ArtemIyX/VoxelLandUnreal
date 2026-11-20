#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandMeshVertex.h"
#include <cmath>

struct FVoxelLandMeshData
{
	TArray<FVoxelLandMeshVertex> Vertices;
	TArray<uint32> Indices;
	FBox3f Bounds = FBox3f(ForceInit);
	uint32 NormalFallbacks = 0;
};

// Mesh-local floats share an exactly representable binary grid relative to a
// similarly snapped double-precision component origin. The grid is chosen
// from the maximum local extent, not from an assumed voxel size.
namespace VoxelLandMeshCoordinates
{
// Keep local fractional-cell interpolation above double precision noise.
// This is a conservative 2^20-voxel envelope, not a measured M1 limit.
inline bool IsKeyInPrecisionRange(const FIntVector& Key)
{
	constexpr int32 MaxChunk = 32766;
	return Key.X >= -MaxChunk && Key.X <= MaxChunk && Key.Y >= -MaxChunk && Key.Y <= MaxChunk && Key.Z >= -MaxChunk
		&& Key.Z <= MaxChunk;
}
inline double Grid(float Size)
{
	int Exponent = 0;
	std::frexp(double(Size) * 34.0, &Exponent);
	return std::ldexp(1.0, Exponent - 24);
}
inline double Snap(double Value, double Step)
{
	return std::round(Value / Step) * Step;
}
inline FVector RenderOrigin(const FIntVector& Chunk, float Size)
{
	const double Step = Grid(Size), Width = double(Size) * 32.0;
	return FVector(
		Snap(double(Chunk.X) * Width, Step), Snap(double(Chunk.Y) * Width, Step), Snap(double(Chunk.Z) * Width, Step));
}
}
