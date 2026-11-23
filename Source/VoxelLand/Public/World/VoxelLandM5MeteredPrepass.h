#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4WorldSnapshot;
class FVoxelLandM5Budget;
class FVoxelLandM5PrepassBytes;

// Additive M5 prepass-key encoder. It reloads and validates all three programs
// through the metered built-in provider path; source/payload and input lifetime
// must remain immutable for the entire call. Height-stage v1 projection
// comparison requires empty root and function parameter arrays: arbitrary
// FText histories remain Unproven. This creates bytes only: it neither generates
// terrain nor authorizes cache reuse.
class VOXELLAND_API FVoxelLandM5MeteredPrepass final
{
public:
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM4WorldSnapshot& Snapshot, uint32 EncodingVersion,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe>& OutBytes);
};
