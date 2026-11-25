#pragma once

#include "CoreMinimal.h"

// Bounded inspection profile, not a claim of visual acceptance or streaming coverage.
namespace VoxelLandM4Inspection
{
constexpr int32 VoxelSizeCm = 800;
constexpr int32 ChunkSizeCm = 32 * VoxelSizeCm;
constexpr int32 MaxVerticalChunks = 16;
constexpr uint64 MaxMeshBytes = 64ull * 1024 * 1024;
constexpr float HorizontalFovDegrees = 20.f;

inline FVector CameraLocation(const FVector& Target)
{
	return Target + FVector(-200000.0, 0.0, 5000.0);
}

inline int64 FloorDivide(int64 Value, int64 Divisor)
{
	const int64 Quotient = Value / Divisor;
	return Quotient - ((Value % Divisor) < 0 ? 1 : 0);
}

// All global height samples are bounded by [Min, Max]. Bilinear interpolation
// cannot overshoot. Height detail can shift it by DetailAmplitudeQ8, and the
// overhang displacement is zero outside its band around that shifted height.
// Two voxels of clearance cover the existing filter and sampled normals.
// Reject rather than silently clip if the cap is insufficient.
inline bool VerticalChunkRange(
	int32 MinHeightQ8, int32 MaxHeightQ8, int32 BandQ8, int32 DetailAmplitudeQ8, int32& OutMinZ, int32& OutMaxZ)
{
	if (MinHeightQ8 > MaxHeightQ8 || BandQ8 < 0 || BandQ8 > 1280000 || DetailAmplitudeQ8 < 0
		|| DetailAmplitudeQ8 > 1280000)
	{
		return false;
	}
	const int64 Margin = int64(BandQ8) + DetailAmplitudeQ8 + int64(2 * VoxelSizeCm) * 256;
	const int64 ChunkQ8 = int64(ChunkSizeCm) * 256;
	const int64 MinZ = FloorDivide(int64(MinHeightQ8) - Margin, ChunkQ8);
	const int64 MaxZ = FloorDivide(int64(MaxHeightQ8) + Margin, ChunkQ8);
	if (MaxZ - MinZ + 1 > MaxVerticalChunks || MinZ < MIN_int32 || MaxZ > MAX_int32
		|| MinZ * ChunkSizeCm - 2 * VoxelSizeCm < -2000000 || MaxZ * ChunkSizeCm + 34 * VoxelSizeCm > 2000000)
	{
		return false;
	}
	OutMinZ = int32(MinZ);
	OutMaxZ = int32(MaxZ);
	return true;
}
}
