#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM4StageNode.h"
#include "Data/Structs/VoxelLandM4StageOutput.h"

namespace VoxelLandM4StageAnalysis
{
// Validates the entire caller-classified graph and collects the requested global
// output's transitive producer closure, including the producer itself. Results
// are in deterministic dependency-first order, with GUID ordering for ties.
// Outputs are unchanged on failure. This does not validate operators, providers,
// parameter values, stage versions, or constitute a complete cache key.
VOXELLANDALGO_API FVoxelLandError CollectGlobalDependencies(TConstArrayView<FVoxelLandM4StageNode> Nodes,
	TConstArrayView<FVoxelLandM4StageOutput> Outputs, const FString& OutputName, TArray<FGuid>& OutDependencies);
}
