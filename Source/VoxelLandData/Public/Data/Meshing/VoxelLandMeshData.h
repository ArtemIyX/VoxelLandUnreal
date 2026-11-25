#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMeshVertex.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"
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
inline bool TryGetLevelSpacing(float BaseVoxelSizeCm, uint8 Lod, float& OutSpacingCm)
{
	constexpr uint8 MaxPlannedLod = 5;
	if (Lod > MaxPlannedLod || !FMath::IsFinite(BaseVoxelSizeCm) || BaseVoxelSizeCm <= 0.f)
	{
		return false;
	}
	const float Spacing = BaseVoxelSizeCm * float(1 << Lod);
	if (!FMath::IsFinite(Spacing * 34.f))
	{
		return false;
	}
	OutSpacingCm = Spacing;
	return true;
}

// Keep local fractional-cell interpolation above double precision noise.
// This is a conservative 2^20-voxel envelope, not a measured M1 limit.
inline bool IsKeyInPrecisionRange(const FIntVector& Key)
{
	constexpr int32 MaxChunk = 32766;
	return Key.X >= -MaxChunk && Key.X <= MaxChunk && Key.Y >= -MaxChunk && Key.Y <= MaxChunk && Key.Z >= -MaxChunk
		&& Key.Z <= MaxChunk;
}
// The opt-in chunk-local M4 path uses integer-centimeter voxels. Its larger
// envelope keeps global cell coordinates below 2^27, so double additions retain
// at least 25 fractional bits. The binary snap grid is no finer than 2^-19
// voxel; snapped coordinates and local float offsets remain exactly representable.
// This is a CPU meshing envelope, not a guarantee about Unreal's world bounds.
inline bool IsUnlimitedM4KeyInPrecisionRange(const FIntVector& Key, float Size)
{
	constexpr int32 MaxChunk = (1 << 22) - 2;
	return FMath::IsFinite(Size) && Size >= 1.f && Size <= 134217.f
		&& FMath::FloorToDouble(double(Size)) == double(Size) && Key.X >= -MaxChunk && Key.X <= MaxChunk
		&& Key.Y >= -MaxChunk && Key.Y <= MaxChunk && Key.Z >= -MaxChunk && Key.Z <= MaxChunk;
}
inline bool IsKeyInPrecisionRange(const FIntVector& Key, float Size, bool bUnlimitedM4)
{
	return bUnlimitedM4 ? IsUnlimitedM4KeyInPrecisionRange(Key, Size) : IsKeyInPrecisionRange(Key);
}
inline bool IsMeshKeyInPrecisionRange(const FVoxelLandChunkKey& Key, float BaseVoxelSizeCm, bool bUnlimitedM4)
{
	float LevelSpacing = 0.f;
	if (!TryGetLevelSpacing(BaseVoxelSizeCm, Key.LOD, LevelSpacing))
	{
		return false;
	}
	if (!bUnlimitedM4)
	{
		return IsKeyInPrecisionRange(Key.Coordinate);
	}
	if (!IsUnlimitedM4KeyInPrecisionRange(FIntVector::ZeroValue, BaseVoxelSizeCm))
	{
		return false;
	}
	constexpr int64 MaxBaseChunk = (int64(1) << 22) - 2;
	const int64 Stride = int64(1) << Key.LOD;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 First = int64(Key.Coordinate[Axis]) * Stride;
		if (First < -MaxBaseChunk || First + Stride - 1 > MaxBaseChunk)
		{
			return false;
		}
	}
	return true;
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
