#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNodeSpec.h"

// Describes builtins only. External operators and function calls remain separate contracts.
class VOXELLANDDATA_API FVoxelLandBuiltinCatalog
{
public:
	// The requested count applies only to Add and Multiply, where it must be in [2, 32].
	// Unknown IDs or invalid variadic counts return false and reset OutSpec.
	static bool Find(EVoxelLandBuiltinNode Kind, FVoxelLandBuiltinNodeSpec& OutSpec, int32 VariadicInputs = 2);
	static void GetKinds(TArray<EVoxelLandBuiltinNode>& OutKinds);
	// Validate static definitions at compile/editor boundaries, never per sample.
	static bool Validate(FString& OutError);
};
