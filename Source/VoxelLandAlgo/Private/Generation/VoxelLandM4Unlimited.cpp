#include "Generation/VoxelLandM4Unlimited.h"

#include "Data/Structs/VoxelLandM4LatticeGeometry.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4Control.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4HeightPrepass.h"

namespace VoxelLandM4Unlimited
{
class FVoxelLandM4UnlimitedInternals final
{
public:
	static FVoxelLandError M4UnlimitedError(EVoxelLandErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandError Error;
		Error.Code = Code;
		Error.Message = Message;
		return Error;
	}
};

FVoxelLandError ValidateChunkDemand(const FVoxelLandChunkKey& Key, float VoxelSizeCm)
{
	if (Key.LOD != 0)
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::UnsupportedLod, TEXT("M4 local control terrain supports only LOD 0"));
	}
	constexpr double MaxVoxelSizeCm = 134217.0;
	const double VoxelSize = static_cast<double>(VoxelSizeCm);
	if (!FMath::IsFinite(VoxelSize) || VoxelSize < 1.0 || VoxelSize > MaxVoxelSizeCm
		|| FMath::FloorToDouble(VoxelSize) != VoxelSize)
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 local voxel size"));
	}
	if (!VoxelLandMeshCoordinates::IsUnlimitedM4KeyInPrecisionRange(Key.Coordinate, VoxelSizeCm))
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(EVoxelLandErrorCode::NonFiniteCoordinate,
			TEXT("M4 local terrain exceeds the CPU meshing coordinate envelope"));
	}

	constexpr int32 FilterHalo = 1;
	constexpr int32 StoredPadding = 1;
	constexpr int64 MaxHeightCm = 2000000;
	constexpr int32 Padding = StoredPadding + FilterHalo;
	const int64 BaseZ = static_cast<int64>(Key.Coordinate.Z) * FVoxelLandChunkData::Cells;
	const int64 SizeCm = static_cast<int64>(VoxelSize);
	const int64 MinZ = (BaseZ - Padding) * SizeCm;
	const int64 MaxZ = (BaseZ + FVoxelLandChunkData::Cells + Padding) * SizeCm;
	if (MinZ < -MaxHeightCm || MaxZ > MaxHeightCm)
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 local terrain Z exceeds the fixed-point range"));
	}
	return {};
}

FVoxelLandError GenerateChunk(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4UnlimitedConfig& Config, const FVoxelLandChunkKey& Key,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled)
{
	const float VoxelSize = FinalSettings.Profile.VoxelSizeCm;
	if (const FVoxelLandError Error = ValidateChunkDemand(Key, VoxelSize))
	{
		return Error;
	}
	if (!FinalSettings.Program)
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 local terrain requires a final program"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 local terrain cancelled"));
	}
	if (!Config.ContinentalGenerator || !Config.MountainGenerator || !Config.PeaksGenerator || !Config.ContinentalCurve
	    || !Config.MountainCurve || !Config.PeaksCurve)
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(EVoxelLandErrorCode::InvalidParameter,
			TEXT("M4 local terrain requires all three control generators and curves"));
	}
	// Always cover the largest supported footprint: stored [-1,33], plus a
	// one-cell Smooth Density halo and one-voxel height-gradient stencil.
	constexpr int32 Start = -3;
	constexpr int32 Cells = 38;
	const int64 SizeCm = int64(VoxelSize);
	int64 OriginX = 0, OriginY = 0;
	if (!VoxelLandM4Control::LatticePositionCm(
			int64(Key.Coordinate.X) * FVoxelLandChunkData::Cells + Start, SizeCm, OriginX)
		|| !VoxelLandM4Control::LatticePositionCm(
			int64(Key.Coordinate.Y) * FVoxelLandChunkData::Cells + Start, SizeCm, OriginY))
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 local terrain coordinate overflows"));
	}
	FVoxelLandM4DomainSettings Region;
	Region.bGlobalErosion = false;
	Region.OriginXCm = OriginX;
	Region.OriginYCm = OriginY;
	Region.CellsX = Cells;
	Region.CellsY = Cells;
	Region.GridSpacingCm = SizeCm;
	const FVoxelLandM4LatticeGeometry Geometry{OriginX, OriginY, SizeCm, Cells, Cells};
	TArray<uint16> Continental, Mountain, Peaks;
	const FVoxelLandGenerator* Generators[] = {
		Config.ContinentalGenerator.Get(), Config.MountainGenerator.Get(), Config.PeaksGenerator.Get()};
	TArray<uint16>* Outputs[] = {&Continental, &Mountain, &Peaks};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Generators); ++Index)
	{
		const FVoxelLandError Error =
			VoxelLandM4FixedLattice::Evaluate(*Generators[Index], Geometry, *Outputs[Index], IsCancelled);
		if (Error)
		{
			return Error;
		}
	}

	TArray<int32> Heights;
	if (const FVoxelLandError Error =
		VoxelLandM4HeightPrepass::BuildBaseHeights(Region, Continental, *Config.ContinentalCurve, Mountain,
			*Config.MountainCurve, Peaks, *Config.PeaksCurve, Config.BaseQ8, Heights, IsCancelled))
	{
		return Error;
	}
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	bool bCancelled = false;
	auto GridCancelled = [&IsCancelled, &bCancelled]() {
		bCancelled |= IsCancelled();
		return bCancelled;
	};
	if (!FVoxelLandM4HeightGrid::Create(Geometry, Heights, Grid, GridCancelled))
	{
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			bCancelled ? EVoxelLandErrorCode::Cancelled : EVoxelLandErrorCode::InvalidParameter,
			bCancelled ? TEXT("M4 local terrain cancelled") : TEXT("Invalid M4 local height grid"));
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FVoxelLandGeneratorSettings ShapingSettings = FinalSettings;
	ShapingSettings.bM4FixedLatticeShaping = true;
	const FVoxelLandError CreateError =
		Config.SmoothOverride.IsSet()
			? FVoxelLandGenerator::CreateM4HeightField(ShapingSettings, Grid, Config.bNormalizeSlope, Config.Overhang,
				Config.SmoothOverride.GetValue(), Config.HeightDetail, Generator)
			: FVoxelLandGenerator::CreateM4HeightField(
				ShapingSettings, Grid, Config.bNormalizeSlope, Config.Overhang, Config.HeightDetail, Generator);
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
		return FVoxelLandM4UnlimitedInternals::M4UnlimitedError(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 local terrain cancelled"));
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}
}