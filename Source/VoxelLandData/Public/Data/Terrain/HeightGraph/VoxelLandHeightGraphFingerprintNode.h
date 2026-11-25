#pragma once

#include "CoreMinimal.h"

// Caller-provided canonical semantics for one graph node. Do not pass editor
// layout or cosmetic material bytes. Version and bytes must cover the operator,
// input pin bindings and defaults, parameters, seed use, and provider semantics.
struct FVoxelLandM4StageFingerprintNode
{
	FGuid Id;
	uint32 SemanticsVersion = 0;
	TArray<uint8> SemanticBytes;
};
