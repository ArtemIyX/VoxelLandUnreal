#pragma once

#include "CoreMinimal.h"

struct FVoxelLandCapsuleSweepRequest
{
	static constexpr int32 MaxCellBudget = 32768;
	static constexpr int32 MaxRegionBudget = 65536;
	static constexpr int32 MaxSubdivisionDepth = 12;

	// Logical world centimeters. Half-height includes the capsule caps.
	FVector3d Start = FVector3d::ZeroVector;
	FVector3d End = FVector3d::ZeroVector;
	double RadiusCm = 34.0;
	double HalfHeightCm = 88.0;
	double SkinCm = 1.0;
	double ContactToleranceCm = 0.25;
	int32 CellBudget = MaxCellBudget;
	int32 RegionBudget = MaxRegionBudget;
	int32 SubdivisionDepth = MaxSubdivisionDepth;
};
