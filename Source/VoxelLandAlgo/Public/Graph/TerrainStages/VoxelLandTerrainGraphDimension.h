#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightGraphStageNode.h"

namespace VoxelLandM4GraphDimension
{
// Conservatively proves that the selected node's upstream branch consists of
// supported M3 builtins that cannot consume Z or perform a spatial filter.
// The output contains only that branch, with one dependency per producer node;
// original pin bindings remain in Snapshot and MUST be included separately in
// any semantic fingerprint. This proof is not a graph compiler, does not check
// parameter/provider semantics or establish exact lattice evaluation, and MUST
// accompany independent compilation/validation before use. It does not alter
// M3, schedule global stages, or access UObjects. Failure leaves OutNodes intact.
VOXELLANDALGO_API FVoxelLandError CollectProven2DBranch(
	const FVoxelLandGraphSnapshot& Snapshot, const FGuid& RootNode, TArray<FVoxelLandM4StageNode>& OutNodes);
}
