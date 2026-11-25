#include "Generation/Terrain/Controls/VoxelLandControlPipeline.h"

#include "Data/Terrain/Domains/VoxelLandTerrainLatticeGeometry.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipelineInputs.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightPrepass.h"

namespace VoxelLandM4ControlPipeline
{
class FVoxelLandM4ControlPipelineInternals final
{
public:
	static FVoxelLandError M4ControlPipelineError(EVoxelLandErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandError Error;
		Error.Code = Code;
		Error.Message = Message;
		return Error;
	}
};

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, const FVoxelLandGenerator& ContinentalGenerator,
	FVoxelLandWorkerContext& ContinentalWorker, const FVoxelLandM4CurveLut& ContinentalCurve,
	const FVoxelLandGenerator& MountainGenerator, FVoxelLandWorkerContext& MountainWorker,
	const FVoxelLandM4CurveLut& MountainCurve, const FVoxelLandGenerator& PeaksGenerator,
	FVoxelLandWorkerContext& PeaksWorker, const FVoxelLandM4CurveLut& PeaksCurve, int32 BaseQ8, uint64 WorldSeed,
	const FGuid& StageId, uint32 Salt, int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled)
{
	return Generate(Domain, ContinentalGenerator, ContinentalWorker, ContinentalCurve, MountainGenerator,
		MountainWorker, MountainCurve, PeaksGenerator, PeaksWorker, PeaksCurve, BaseQ8, WorldSeed, StageId, Salt,
		DropletCount, Erosion, OutGrid, OutHash, IsCancelled, nullptr);
}

FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain, const FVoxelLandGenerator& ContinentalGenerator,
	FVoxelLandWorkerContext& ContinentalWorker, const FVoxelLandM4CurveLut& ContinentalCurve,
	const FVoxelLandGenerator& MountainGenerator, FVoxelLandWorkerContext& MountainWorker,
	const FVoxelLandM4CurveLut& MountainCurve, const FVoxelLandGenerator& PeaksGenerator,
	FVoxelLandWorkerContext& PeaksWorker, const FVoxelLandM4CurveLut& PeaksCurve, int32 BaseQ8, uint64 WorldSeed,
	const FGuid& StageId, uint32 Salt, int32 DropletCount, const FVoxelLandM4ErosionSettings& Erosion,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease)
{
	return Generate({Domain, {ContinentalGenerator, ContinentalWorker, ContinentalCurve},
						{MountainGenerator, MountainWorker, MountainCurve}, {PeaksGenerator, PeaksWorker, PeaksCurve},
						BaseQ8, WorldSeed, StageId, Salt, DropletCount, Erosion},
		OutGrid, OutHash, IsCancelled, MoveTemp(Lease));
}

FVoxelLandError Generate(const FVoxelLandM4ControlPipelineInputs& Inputs,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease)
{
	const FVoxelLandM4DomainSettings& Domain = Inputs.Domain;
	if (const FVoxelLandError Error = VoxelLandM4Domain::Validate(Domain))
	{
		return Error;
	}
	if (Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return FVoxelLandM4ControlPipelineInternals::M4ControlPipelineError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 control prepass requires a finite domain"));
	}
	if (const FVoxelLandError Error =
			VoxelLandM4ControlGrid::ValidateProgram(Inputs.Continental.Generator.GetProgram()))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(Inputs.Mountain.Generator.GetProgram()))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(Inputs.Peaks.Generator.GetProgram()))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ControlPipelineInternals::M4ControlPipelineError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 control prepass cancelled"));
	}

	const FVoxelLandM4LatticeGeometry Geometry{
		Domain.OriginXCm, Domain.OriginYCm, Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY};
	TArray<uint16> Continental, Mountain, Peaks;
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::Evaluate(
			Inputs.Continental.Generator, Inputs.Continental.Worker, Geometry, Continental, IsCancelled))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::Evaluate(
			Inputs.Mountain.Generator, Inputs.Mountain.Worker, Geometry, Mountain, IsCancelled))
	{
		return Error;
	}
	if (const FVoxelLandError Error =
			VoxelLandM4ControlGrid::Evaluate(Inputs.Peaks.Generator, Inputs.Peaks.Worker, Geometry, Peaks, IsCancelled))
	{
		return Error;
	}

	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	if (const FVoxelLandError Error = VoxelLandM4HeightPrepass::Generate(Domain, Continental, Inputs.Continental.Curve,
			Mountain, Inputs.Mountain.Curve, Peaks, Inputs.Peaks.Curve, Inputs.BaseQ8, Inputs.WorldSeed, Inputs.StageId,
			Inputs.Salt, Inputs.DropletCount, Inputs.Erosion, Grid, Hash, IsCancelled, Lease))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ControlPipelineInternals::M4ControlPipelineError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 control prepass cancelled"));
	}
	OutGrid = MoveTemp(Grid);
	OutHash = Hash;
	return {};
}
}