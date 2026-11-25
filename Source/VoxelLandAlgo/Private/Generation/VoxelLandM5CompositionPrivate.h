#pragma once
#include "CoreMinimal.h"
#include "Templates/Function.h"
struct FVoxelLandM5CavePlan;
struct FVoxelLandM5PointCm;
namespace VoxelLand::M5::Composition::Private
{
bool TryComposeOwned(int16 M4Density, const FVoxelLandM5CavePlan& Plan,
	const FVoxelLandM5PointCm& PositionCm, int32 VoxelSizeCm, bool bEnabled, bool bCarveAllowed, bool bProtected,
	bool bHasPocket, int64 PocketDistanceQ8, TFunctionRef<bool()> IsCancelled, int16& Out);
}
