#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM4DomainSettings.h"
#include "Data/Structs/VoxelLandM4ErosionSettings.h"

class FVoxelLandM4CurveLut;
class FVoxelLandM4HeightGrid;

/** An opt-in, finite control-map to height prepass. This is not graph compilation,
 * world initialization, or a cache dependency key. Each LUT output is a signed
 * Q8-centimeter height term; normalized inputs are uint16. At each lattice point
 * the fixed expression is BaseQ8 + ContinentalCurve(C) + MountainCurve(M) +
 * PeaksCurve(P), evaluated in that order with int64 intermediates. The final
 * value must be within the M4 height range; no saturation or floating point is
 * used. LUT baking, control graph selection and world format are caller-owned.
 */
namespace VoxelLandM4HeightPrepass
{
VOXELLANDALGO_API FVoxelLandError BuildBaseHeights(const FVoxelLandM4DomainSettings& Domain,
	TConstArrayView<uint16> Continental, const FVoxelLandM4CurveLut& ContinentalCurve, TConstArrayView<uint16> Mountain,
	const FVoxelLandM4CurveLut& MountainCurve, TConstArrayView<uint16> Peaks, const FVoxelLandM4CurveLut& PeaksCurve,
	int32 BaseQ8, TArray<int32>& OutHeights, TFunctionRef<bool()> IsCancelled);

// Build the same base grid and feed it to the existing finite erosion prepass.
// Both outputs remain unchanged on validation failure or cancellation.
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	TConstArrayView<uint16> Continental, const FVoxelLandM4CurveLut& ContinentalCurve, TConstArrayView<uint16> Mountain,
	const FVoxelLandM4CurveLut& MountainCurve, TConstArrayView<uint16> Peaks, const FVoxelLandM4CurveLut& PeaksCurve,
	int32 BaseQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount,
	const FVoxelLandM4ErosionSettings& Erosion, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash, TFunctionRef<bool()> IsCancelled);
}
