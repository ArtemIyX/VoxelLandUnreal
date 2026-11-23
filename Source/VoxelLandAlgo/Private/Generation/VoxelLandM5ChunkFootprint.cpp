#include "Generation/VoxelLandM5ChunkFootprint.h"

#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5CheckedMath.h"

class FVoxelLandM5ChunkFootprintInternals final
{
public:
	static constexpr int64 Cells = 32;
	static constexpr int64 StoredPadding = 1;
	static constexpr int64 SmoothHalo = 1;
	static constexpr int64 SlopeStencil = 1;

	static bool Axis(int32 Chunk, int64 Size, int64 Padding, int64& OutMin, int64& OutMax)
	{
		int64 Base = 0;
		int64 Min = 0;
		int64 Max = 0;
		return VoxelLand::M5::CheckedMath::TryMultiply(Chunk, Cells, Base)
			&& VoxelLand::M5::CheckedMath::TrySubtract(Base, Padding, Min)
			&& VoxelLand::M5::CheckedMath::TryAdd(Base, Cells + Padding, Max)
			&& VoxelLand::M5::CheckedMath::TryMultiply(Min, Size, OutMin)
			&& VoxelLand::M5::CheckedMath::TryMultiply(Max, Size, OutMax);
	}
};

bool VoxelLand::M5::ChunkFootprint::TryBuild(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm, bool bSmooth,
	bool bNormalizeSlope, FVoxelLandM5ChunkFootprint& Out)
{
	if (Key.LOD != 0 || VoxelSizeCm <= 0)
	{
		return false;
	}
	const int64 Stored = FVoxelLandM5ChunkFootprintInternals::StoredPadding;
	const int64 Density = Stored + (bSmooth ? FVoxelLandM5ChunkFootprintInternals::SmoothHalo : 0);
	const int64 Height = Density + (bNormalizeSlope ? FVoxelLandM5ChunkFootprintInternals::SlopeStencil : 0);
	FVoxelLandM5ChunkFootprint Result;
	if (!FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Stored, Result.StoredMin.X, Result.StoredMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Stored, Result.StoredMin.Y, Result.StoredMax.Y)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Z, VoxelSizeCm, Stored, Result.StoredMin.Z, Result.StoredMax.Z)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Density, Result.DensityInputMin.X, Result.DensityInputMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Density, Result.DensityInputMin.Y, Result.DensityInputMax.Y)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Z, VoxelSizeCm, Density, Result.DensityInputMin.Z, Result.DensityInputMax.Z)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Height, Result.HeightInputMin.X, Result.HeightInputMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Height, Result.HeightInputMin.Y, Result.HeightInputMax.Y))
	{
		return false;
	}
	Result.HeightInputMin.Z = Result.DensityInputMin.Z;
	Result.HeightInputMax.Z = Result.DensityInputMax.Z;
	Out = Result;
	return true;
}
