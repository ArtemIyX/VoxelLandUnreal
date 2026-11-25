#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainSettings.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainFootprint.h"

namespace VoxelLandM4Domain
{
// Finite grid endpoint arithmetic, sample count, and mode/erosion compatibility
// are validated before allocations. Unlimited worlds cannot use global erosion.
VOXELLANDALGO_API FVoxelLandError Validate(const FVoxelLandM4DomainSettings& Settings);

// Validate the full inclusive XY sample footprint after padding, filter halo,
// and source/stencil margins. Even unlimited mode rejects arithmetic overflow;
// it has no finite XY clipping. No world or chunk manager is modified.
VOXELLANDALGO_API FVoxelLandError ValidateFootprint(
	const FVoxelLandM4DomainSettings& Settings, const FVoxelLandM4DomainFootprint& Footprint);
}
