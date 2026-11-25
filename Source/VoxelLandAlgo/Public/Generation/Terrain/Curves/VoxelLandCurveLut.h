#pragma once

#include "CoreMinimal.h"

// Immutable evaluator for already-quantized M4 curve samples. This does not
// define authoring, baking, serialization, output units, or a world format.
class VOXELLANDALGO_API FVoxelLandM4CurveLut final
{
public:
	static constexpr int32 SampleCount = 4097;

	// The caller supplies the documented bounds for its chosen output units.
	// The complete LUT is copied only after its size and bounds are validated.
	static bool Create(TConstArrayView<int32> Samples, int32 Minimum, int32 Maximum,
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& OutCurve, FString& OutError);

	// Input represents normalized [0,1] as [0,65535]. Interpolation uses exact
	// integer weights, then rounds once to nearest, ties away from zero.
	int32 Evaluate(uint16 Input) const;

private:
	FVoxelLandM4CurveLut() = default;
	TArray<int32> Values;
};
