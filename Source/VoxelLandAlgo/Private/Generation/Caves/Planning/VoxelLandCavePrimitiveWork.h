#pragma once

#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

// A deterministic upper bound in logical units for each primitive visit.
// Profile segments perform one 30-step distance root and two 30-step plane
// interpolations; chambers perform one 30-step root. Both field and carve use
// these identical costs, including work admitted for samples later masked out.
namespace VoxelLand::M5::PrimitiveWork
{
inline bool TryForPlan(const FVoxelLandM5CavePlan& Plan, int64& Out)
{
	using namespace CheckedMath;
	if (Plan.GeometryVersion != 1 && Plan.GeometryVersion != 2)
	{
		return false;
	}
	int64 A = 0, B = 0, Total = 0;
	const int64 ChamberCost = Plan.GeometryVersion == 1 ? 1 : 64;
	const int64 SegmentCost = Plan.GeometryVersion == 1 ? 1 : 128;
	if (!TryMultiplyNonNegative(Plan.Chambers.Num(), ChamberCost, A)
		|| !TryMultiplyNonNegative(Plan.Segments.Num(), SegmentCost, B) || !TryAdd(A, B, Total)
		|| !TryAdd(Total, 1, Total))
	{
		return false;
	}
	Out = Total;
	return true;
}
}
