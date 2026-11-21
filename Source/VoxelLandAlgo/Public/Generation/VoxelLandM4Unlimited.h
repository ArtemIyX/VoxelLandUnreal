#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM4OverhangConfig.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "Data/Structs/VoxelLandM4SmoothSettings.h"
#include "Generation/VoxelLandGeneratorSettings.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
class FVoxelLandM4CurveLut;

// Chunk-local M4 generation without a global erosion grid. Control workers must
// belong to their respective generators and be created, used, and destroyed on
// the invoking non-game thread. Each control output is evaluated at global XY
// integer coordinates; its curves contribute Q8-centimeter heights in fixed
// continental/mountain/peaks order. No adjacent chunk results are read.
//
// The entire smoothing + slope halo is sampled afresh for every chunk. This is
// limited to LOD 0 and the existing VM's exact integer float range of +/-2^24 cm;
// it is not a mathematically infinite world. Integer voxel size must also fit
// the current height-grid interpolation bound. No erosion or global prepass is
// performed. The final source program contributes only its optional Smooth
// Density filter, unless explicitly overridden; its pure VM output is not used. On failure or cancellation,
// OutChunk remains unchanged. No world streaming integration is provided here.
namespace VoxelLandM4Unlimited
{
VOXELLANDALGO_API FVoxelLandError GenerateChunk(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4OverhangConfig& Overhang, bool bNormalizeSlope, int32 BaseQ8,
	const FVoxelLandGenerator& ContinentalGenerator, FVoxelLandWorkerContext& ContinentalWorker,
	const FVoxelLandM4CurveLut& ContinentalCurve, const FVoxelLandGenerator& MountainGenerator,
	FVoxelLandWorkerContext& MountainWorker, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, FVoxelLandWorkerContext& PeaksWorker,
	const FVoxelLandM4CurveLut& PeaksCurve, const FVoxelLandChunkKey& Key,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled,
	TOptional<FVoxelLandM4SmoothSettings> SmoothOverride = TOptional<FVoxelLandM4SmoothSettings>(),
	const FVoxelLandM4HeightDetailConfig& HeightDetail = FVoxelLandM4HeightDetailConfig());
}
