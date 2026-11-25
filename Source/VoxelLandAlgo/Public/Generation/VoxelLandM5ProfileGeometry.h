#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Version 2 floor/ceiling geometry. These native inputs are proposals only:
// they do not authorize a plan, establish influence, or prove capsule fit.
namespace VoxelLand::M5::ProfileGeometry
{
struct FSegment final
{
	int32 Version = 2;
	FVoxelLandM5PointCm StartCm;
	FVoxelLandM5PointCm EndCm;
	int32 RadiusCm = 0;
	int64 StartFloorCm = 0;
	int64 EndFloorCm = 0;
	int64 StartCeilingCm = 0;
	int64 EndCeilingCm = 0;
};

struct FChamber final
{
	int32 Version = 2;
	FVoxelLandM5PointCm CenterCm;
	int32 RadiusCm = 0;
	int64 FloorCm = 0;
	int64 CeilingCm = 0;
};

// Negative denotes cave volume. Lateral distance is the exact fixed-point XY
// capsule/circle distance, intersected with the interpolated floor and ceiling.
// Coordinates and heights are bounded to +/-1048576 cm. Zero XY length, an
// unsupported version, or invalid bounds fails without changing OutDistanceQ8.
VOXELLANDALGO_API bool TrySegmentDistanceQ8(
	const FSegment& Segment, const FVoxelLandM5PointCm& PositionCm, int64& OutDistanceQ8);
VOXELLANDALGO_API bool TryChamberDistanceQ8(
	const FChamber& Chamber, const FVoxelLandM5PointCm& PositionCm, int64& OutDistanceQ8);
}
