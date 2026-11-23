#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ScratchChunks;

namespace VoxelLand::M5::ScratchProtectionCheck
{
// Checks the 4x4x4 contributing samples around each protected owned cell.
// Inputs must be immutable, share Budget, and match the baseline, protection
// grid and flags used to compose Chunks. A supplied binary grid must cover
// every owned cell; an absent optional grid adds no protection. This is not a
// roof, opening, water, full mesher-influence, provenance or publication proof.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5BaselineWindow& Baseline,
	const FVoxelLandM5ScratchChunks& Chunks, const FVoxelLandM5ProtectionGrid* Protection,
	bool bRequireMask, bool bProtectAll, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled);
}
