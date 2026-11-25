#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Operators/VoxelLandOperator.h"

FVoxelLandM5DefinitionInputs::FVoxelLandM5DefinitionInputs(
	TVoxelLandM5ConstRef<FVoxelLandM5WorldSnapshot> InSnapshot,
	TVoxelLandM5ConstPtr<FVoxelLandM5ProtectionGrid> InProtection,
	TVoxelLandM5ConstRef<FVoxelLandOperatorRegistrySnapshot> InRegistry,
	const FVoxelLandM5Bounds& InEffectiveBounds, bool bInExplicitBounds, bool bInAuthoritativeDryWorld)
	: Snapshot(MoveTemp(InSnapshot)), Protection(MoveTemp(InProtection)), Registry(MoveTemp(InRegistry)),
	  EffectiveBounds(InEffectiveBounds), bExplicitBounds(bInExplicitBounds), bAuthoritativeDryWorld(bInAuthoritativeDryWorld)
{
}
FVoxelLandM5DefinitionInputs::~FVoxelLandM5DefinitionInputs() = default;
