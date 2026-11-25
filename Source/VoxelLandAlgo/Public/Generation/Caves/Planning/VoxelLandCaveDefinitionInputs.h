#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Geometry/VoxelLandCaveBounds.h"

struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandOperatorRegistrySnapshot;

// Immutable native inputs. The caller supplies authoritative dry-world and
// explicit finite bounds declarations; typed game-world validation is separate.
struct VOXELLANDALGO_API FVoxelLandM5DefinitionInputs
{
	FVoxelLandM5DefinitionInputs(TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
		TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
		TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry,
		const FVoxelLandM5Bounds& InEffectiveBounds, bool bInExplicitBounds, bool bInAuthoritativeDryWorld);
	~FVoxelLandM5DefinitionInputs();
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> Snapshot;
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> Protection;
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> Registry;
	FVoxelLandM5Bounds EffectiveBounds;
	bool bExplicitBounds = false;
	bool bAuthoritativeDryWorld = false;
};
