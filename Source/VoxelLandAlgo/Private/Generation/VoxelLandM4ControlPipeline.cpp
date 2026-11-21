#include "Generation/VoxelLandM4ControlPipeline.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4Domain.h"
#include "Generation/VoxelLandM4HeightPrepass.h"

namespace VoxelLandM4ControlPipeline
{
namespace
{
FVoxelLandError M4ControlPipelineError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}
}

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
	if (const FVoxelLandError Error = VoxelLandM4Domain::Validate(Domain))
	{
		return Error;
	}
	if (Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return M4ControlPipelineError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 control prepass requires a finite domain"));
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(ContinentalGenerator.GetProgram()))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(MountainGenerator.GetProgram()))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(PeaksGenerator.GetProgram()))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return M4ControlPipelineError(EVoxelLandErrorCode::Cancelled, TEXT("M4 control prepass cancelled"));
	}

	TArray<uint16> Continental, Mountain, Peaks;
	if (const FVoxelLandError Error =
			VoxelLandM4ControlGrid::Evaluate(ContinentalGenerator, ContinentalWorker, Domain.OriginXCm,
				Domain.OriginYCm, Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY, Continental, IsCancelled))
	{
		return Error;
	}
	if (const FVoxelLandError Error =
			VoxelLandM4ControlGrid::Evaluate(MountainGenerator, MountainWorker, Domain.OriginXCm, Domain.OriginYCm,
				Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY, Mountain, IsCancelled))
	{
		return Error;
	}
	if (const FVoxelLandError Error = VoxelLandM4ControlGrid::Evaluate(PeaksGenerator, PeaksWorker, Domain.OriginXCm,
			Domain.OriginYCm, Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY, Peaks, IsCancelled))
	{
		return Error;
	}

	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	if (const FVoxelLandError Error =
			VoxelLandM4HeightPrepass::Generate(Domain, Continental, ContinentalCurve, Mountain, MountainCurve, Peaks,
				PeaksCurve, BaseQ8, WorldSeed, StageId, Salt, DropletCount, Erosion, Grid, Hash, IsCancelled, Lease))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return M4ControlPipelineError(EVoxelLandErrorCode::Cancelled, TEXT("M4 control prepass cancelled"));
	}
	OutGrid = MoveTemp(Grid);
	OutHash = Hash;
	return {};
}
}
