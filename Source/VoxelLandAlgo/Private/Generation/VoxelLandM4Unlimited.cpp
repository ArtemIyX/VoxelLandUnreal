#include "Generation/VoxelLandM4Unlimited.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4Control.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4HeightPrepass.h"

namespace VoxelLandM4Unlimited
{
namespace
{
FVoxelLandError M4UnlimitedError(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}
}

static FVoxelLandError GenerateChunkInternal(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4OverhangConfig& Overhang, bool bNormalizeSlope, int32 BaseQ8,
	const FVoxelLandGenerator& ContinentalGenerator, FVoxelLandWorkerContext* ContinentalWorker,
	const FVoxelLandM4CurveLut& ContinentalCurve, const FVoxelLandGenerator& MountainGenerator,
	FVoxelLandWorkerContext* MountainWorker, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, FVoxelLandWorkerContext* PeaksWorker,
	const FVoxelLandM4CurveLut& PeaksCurve, const FVoxelLandChunkKey& Key,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled,
	TOptional<FVoxelLandM4SmoothSettings> SmoothOverride, const FVoxelLandM4HeightDetailConfig& HeightDetail,
	bool bFixedLattice)
{
	if (Key.LOD != 0)
	{
		return M4UnlimitedError(
			EVoxelLandErrorCode::UnsupportedLod, TEXT("M4 local control terrain supports only LOD 0"));
	}
	const double VoxelSize = double(FinalSettings.Profile.VoxelSizeCm);
	if (!FMath::IsFinite(VoxelSize) || VoxelSize < 1.0 || VoxelSize > 134217.0
	    || FMath::FloorToDouble(VoxelSize) != VoxelSize || !FinalSettings.Program)
	{
		return M4UnlimitedError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 local voxel size or final program"));
	}
	if (IsCancelled())
	{
		return M4UnlimitedError(EVoxelLandErrorCode::Cancelled, TEXT("M4 local terrain cancelled"));
	}
	// Always cover the largest supported footprint: stored [-1,33], plus a
	// one-cell Smooth Density halo and one-voxel height-gradient stencil.
	constexpr int32 Start = -3;
	constexpr int32 Cells = 38;
	const int64 SizeCm = int64(VoxelSize);
	int64 OriginX = 0, OriginY = 0;
	if (!VoxelLandM4Control::LatticePositionCm(int64(Key.Coordinate.X) * 32 + Start, SizeCm, OriginX)
	    || !VoxelLandM4Control::LatticePositionCm(int64(Key.Coordinate.Y) * 32 + Start, SizeCm, OriginY))
	{
		return M4UnlimitedError(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 local terrain coordinate overflows"));
	}
	FVoxelLandM4DomainSettings Region;
	Region.bGlobalErosion = false;
	Region.OriginXCm = OriginX;
	Region.OriginYCm = OriginY;
	Region.CellsX = Cells;
	Region.CellsY = Cells;
	Region.GridSpacingCm = SizeCm;
	TArray<uint16> Continental, Mountain, Peaks;
	const FVoxelLandGenerator* Generators[] = {&ContinentalGenerator, &MountainGenerator, &PeaksGenerator};
	FVoxelLandWorkerContext* Workers[] = {ContinentalWorker, MountainWorker, PeaksWorker};
	TArray<uint16>* Outputs[] = {&Continental, &Mountain, &Peaks};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!bFixedLattice && !Workers[Index])
		{
			return M4UnlimitedError(EVoxelLandErrorCode::InvalidContext, TEXT("M4 local control worker missing"));
		}
		const FVoxelLandError Error = bFixedLattice
			                              ? VoxelLandM4FixedLattice::Evaluate(*Generators[Index], OriginX, OriginY,
				                              SizeCm, Cells, Cells, *Outputs[Index], IsCancelled)
			                              : VoxelLandM4ControlGrid::Evaluate(*Generators[Index], *Workers[Index],
				                              OriginX,
				                              OriginY, SizeCm, Cells, Cells, *Outputs[Index], IsCancelled);
		if (Error)
		{
			return Error;
		}
	}

	TArray<int32> Heights;
	if (const FVoxelLandError Error = VoxelLandM4HeightPrepass::BuildBaseHeights(Region, Continental, ContinentalCurve,
		Mountain, MountainCurve, Peaks, PeaksCurve, BaseQ8, Heights, IsCancelled))
	{
		return Error;
	}
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	bool bCancelled = false;
	auto GridCancelled = [&IsCancelled, &bCancelled]() {
		bCancelled |= IsCancelled();
		return bCancelled;
	};
	if (!FVoxelLandM4HeightGrid::Create(OriginX, OriginY, Cells, Cells, SizeCm, Heights, Grid, GridCancelled))
	{
		return M4UnlimitedError(bCancelled ? EVoxelLandErrorCode::Cancelled : EVoxelLandErrorCode::InvalidParameter,
			bCancelled ? TEXT("M4 local terrain cancelled") : TEXT("Invalid M4 local height grid"));
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FVoxelLandGeneratorSettings ShapingSettings = FinalSettings;
	ShapingSettings.bM4FixedLatticeShaping = bFixedLattice;
	const FVoxelLandError CreateError =
		SmoothOverride.IsSet()
			? FVoxelLandGenerator::CreateM4HeightField(ShapingSettings, Grid, bNormalizeSlope,
				Overhang, SmoothOverride.GetValue(), HeightDetail, Generator)
			: FVoxelLandGenerator::CreateM4HeightField(
				ShapingSettings, Grid, bNormalizeSlope, Overhang, HeightDetail, Generator);
	if (CreateError)
	{
		return CreateError;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Generator->CreateWorker(Worker))
	{
		return Error;
	}
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
	if (const FVoxelLandError Error = Generator->GenerateChunkCancellable(Key, *Worker, Chunk, IsCancelled))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return M4UnlimitedError(EVoxelLandErrorCode::Cancelled, TEXT("M4 local terrain cancelled"));
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}

FVoxelLandError GenerateChunk(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4OverhangConfig& Overhang, bool bNormalizeSlope, int32 BaseQ8,
	const FVoxelLandGenerator& ContinentalGenerator, FVoxelLandWorkerContext& ContinentalWorker,
	const FVoxelLandM4CurveLut& ContinentalCurve, const FVoxelLandGenerator& MountainGenerator,
	FVoxelLandWorkerContext& MountainWorker, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, FVoxelLandWorkerContext& PeaksWorker,
	const FVoxelLandM4CurveLut& PeaksCurve, const FVoxelLandChunkKey& Key,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled,
	TOptional<FVoxelLandM4SmoothSettings> SmoothOverride, const FVoxelLandM4HeightDetailConfig& HeightDetail)
{
	return GenerateChunkInternal(FinalSettings, Overhang, bNormalizeSlope, BaseQ8, ContinentalGenerator,
		&ContinentalWorker, ContinentalCurve, MountainGenerator, &MountainWorker, MountainCurve, PeaksGenerator,
		&PeaksWorker, PeaksCurve, Key, OutChunk, IsCancelled, SmoothOverride, HeightDetail, false);
}

FVoxelLandError GenerateFixedChunk(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4OverhangConfig& Overhang, bool bNormalizeSlope, int32 BaseQ8,
	const FVoxelLandGenerator& ContinentalGenerator, const FVoxelLandM4CurveLut& ContinentalCurve,
	const FVoxelLandGenerator& MountainGenerator, const FVoxelLandM4CurveLut& MountainCurve,
	const FVoxelLandGenerator& PeaksGenerator, const FVoxelLandM4CurveLut& PeaksCurve, const FVoxelLandChunkKey& Key,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled,
	TOptional<FVoxelLandM4SmoothSettings> SmoothOverride, const FVoxelLandM4HeightDetailConfig& HeightDetail)
{
	return GenerateChunkInternal(FinalSettings, Overhang, bNormalizeSlope, BaseQ8, ContinentalGenerator, nullptr,
		ContinentalCurve, MountainGenerator, nullptr, MountainCurve, PeaksGenerator, nullptr, PeaksCurve, Key, OutChunk,
		IsCancelled, SmoothOverride, HeightDetail, true);
}
}