#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Operators/VoxelLandOperator.h"

FVoxelLandM5DefinitionInputs::FVoxelLandM5DefinitionInputs(TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry, const FVoxelLandM5Bounds& InEffectiveBounds,
	bool bInExplicitBounds, bool bInAuthoritativeDryWorld)
	: Snapshot(MoveTemp(InSnapshot))
	, Protection(MoveTemp(InProtection))
	, Registry(MoveTemp(InRegistry))
	, EffectiveBounds(InEffectiveBounds)
	, bExplicitBounds(bInExplicitBounds)
	, bAuthoritativeDryWorld(bInAuthoritativeDryWorld)
{
}
FVoxelLandM5DefinitionInputs::~FVoxelLandM5DefinitionInputs() = default;
