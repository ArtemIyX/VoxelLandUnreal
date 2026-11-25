#pragma once

#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM6Stamp.h"

struct FVoxelLandChunkKey;

class FVoxelLandM6StampMath final
{
public:
	static constexpr int64 LegacySphereLimitCm = 2000000;
	static constexpr int64 CoordinateLimitCm = int64(1) << 45;
	static constexpr int64 MaxStampDimensionCm = 2000000;
	static constexpr int64 MaxCapsuleSpanCm = 2000000;
	static constexpr int64 MaxVoxelSizeCm = 134217;

	static FInt64Vector FirstSampleCm(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm);
	static FVoxelLandError Validate(const FVoxelLandM6Stamp& Stamp, int64 CoordinateLimit);
	static int16 Combine(int16 Density, const FVoxelLandM6Stamp& Stamp, const FInt64Vector& PositionCm,
		int64 VoxelSizeCm);
	static int16 QuantizeDistanceQ8(int64 DistanceQ8, int64 VoxelSizeCm, bool bSubtract);

private:
	static constexpr int64 CentimetreScaleQ8 = 256;
	static constexpr int64 DensityUnitsPerVoxel = 1024;
	static constexpr int64 SaturationRadiusVoxels = 32;
	static constexpr int64 ParameterOneQ32 = int64(1) << 32;

	static bool PositionInRange(const FInt64Vector& Point, int64 Limit);
	static bool DimensionInRange(int64 Size);
	static void GetBounds(const FVoxelLandM6Stamp& Stamp, FInt64Vector& Min, FInt64Vector& Max);
	static uint64 FloorSqrt(uint64 Squared);
	static int64 RoundRatio(int64 Numerator, int64 Denominator);
	static int64 LengthQ8(const FInt64Vector& DeltaQ8);
	static int64 CapsuleParameterQ32(const FInt64Vector& Delta, const FInt64Vector& Segment);
	static FInt64Vector CapsuleClosestOffsetQ8(const FInt64Vector& Delta, const FInt64Vector& Segment);
	static int64 SignedDistanceQ8(const FVoxelLandM6Stamp& Stamp, const FInt64Vector& Position);
};
