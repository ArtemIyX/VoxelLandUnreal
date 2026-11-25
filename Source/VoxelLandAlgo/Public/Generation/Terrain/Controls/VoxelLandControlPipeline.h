#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainSettings.h"
#include "Data/Terrain/Erosion/VoxelLandErosionSettings.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
class FVoxelLandM4CurveLut;
class FVoxelLandM4HeightGrid;
class FVoxelLandM4GridLease;
struct FVoxelLandM4ControlPipelineInputs;

// Opt-in CPU pipeline from three existing single-output VM programs to an
// immutable, finite height prepass. Each generator must own its corresponding
// worker; all three workers must be created, used, and destroyed on the current
// non-game thread. Programs are strictly checked by M4ControlGrid for 2D-only
// bytecode, and coordinates must be exact float integers within +/- 2^24 cm.
// Caller is responsible for instantiating each generator with the correct
// world seed and independent node seed slots; WorldSeed below seeds erosion.
// The curves supply signed Q8 centimeter terms; see M4HeightPrepass for their
// combination order. A failure or cancellation leaves both outputs unchanged.
// This does not implement named graph outputs, caching, preview, or runtime
// world initialization, and the output hash is not a prepass dependency key.
namespace VoxelLandM4ControlPipeline
{
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4ControlPipelineInputs& Inputs,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease = nullptr);

VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	const FVoxelLandGenerator& ContinentalGenerator, FVoxelLandWorkerContext& ContinentalWorker,
	const FVoxelLandM4CurveLut& ContinentalCurve, const FVoxelLandGenerator& MountainGenerator,
	FVoxelLandWorkerContext& MountainWorker, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, FVoxelLandWorkerContext& PeaksWorker,
	const FVoxelLandM4CurveLut& PeaksCurve, int32 BaseQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt,
	int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled);
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	const FVoxelLandGenerator& ContinentalGenerator, FVoxelLandWorkerContext& ContinentalWorker,
	const FVoxelLandM4CurveLut& ContinentalCurve, const FVoxelLandGenerator& MountainGenerator,
	FVoxelLandWorkerContext& MountainWorker, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, FVoxelLandWorkerContext& PeaksWorker,
	const FVoxelLandM4CurveLut& PeaksCurve, int32 BaseQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt,
	int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease);
}