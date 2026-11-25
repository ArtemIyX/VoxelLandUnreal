#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5ProtectionGrid;

namespace VoxelLand::M5::WindowSurfaceQuery
{
// Queries the actual baseline window and conservative protection columns using
// the existing flat-midpoint proof. Both retained inputs must belong to Budget;
// a supplied protection grid must share the voxel lattice and contain every
// incident cell around the full window. bDryWorld is an owner declaration,
// not a water certificate. The caller must establish the complete world-height
// domain, source/identity provenance, mesher/filter influence, roof and final
// field safety. Slopes, ambiguous crossings and incomplete halos fail closed.
// No output is modified on failure or cancellation.
VOXELLANDALGO_API FVoxelLandM5Diagnostic QueryFlat(const FVoxelLandM5BaselineWindow& Window,
	const FVoxelLandM5ProtectionGrid* Protection, bool bProtectAll, bool bDryWorld,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, int64 X, int64 Y, int32 RadiusCm,
	int64 MaxSamplesToInspect, TFunctionRef<bool()> IsCancelled, int64& OutSurfaceZ, int64& OutMinSurfaceZ,
	int64& OutMaxSurfaceZ, int32& OutSlopeDegrees, bool& bOutProtected, bool& bOutFlooded);
}
