#pragma once

#include "CoreMinimal.h"

struct FVoxelLandFiniteWorldSnapshot;
struct FVoxelLandM4WorldSnapshot;

// Translates a bounded, immutable finite capture to the existing M4 input shape.
// This does not validate programs, providers, baked curves, or identity. Caller
// retains and validates the full finite capture, including its cave settings and
// vertical cave bounds, and must independently validate the translated M4 input.
// Input storage must remain unchanged during the call. Failure preserves Out.
class VOXELLAND_API FVoxelLandFiniteSnapshotAdapter final
{
public:
	static bool TryConvert(const FVoxelLandFiniteWorldSnapshot& Source, FVoxelLandM4WorldSnapshot& Out, FString& Error);
};
