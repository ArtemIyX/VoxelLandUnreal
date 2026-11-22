#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4CurveKey.h"
#include "Generation/VoxelLandM4CurveLut.h"

// Provisional, explicit piecewise-linear bake. This does not define an asset
// schema or change existing M3 program semantics.
namespace VoxelLandM4CurveBake
{
// Version 1 requires 2..65536 keys, strictly increasing InputQ16 values,
// including 0 and 65535. All outputs must lie in the caller's declared range.
// LUT element i is the value at normalized position i/4096. Its position is
// compared exactly against key positions InputQ16/65535; adjacent key outputs
// are linearly interpolated in int64 and rounded once to nearest, ties away
// from zero. Output values inherit the caller's units and are never clamped.
// The full int32 output range is supported without intermediate overflow.
// On failure both outputs are unchanged and OutError explains the error.
constexpr uint32 AlgorithmVersion = 1;
VOXELLANDALGO_API bool Bake(TConstArrayView<FVoxelLandM4CurveKey> Keys, int32 Minimum, int32 Maximum,
	TArray<int32>& OutSamples, TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& OutCurve,
	FString& OutError);
}