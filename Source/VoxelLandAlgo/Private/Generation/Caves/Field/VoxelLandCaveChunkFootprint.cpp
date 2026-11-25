#include "Generation/Caves/Field/VoxelLandCaveChunkFootprint.h"

#include "Data/Field/Chunks/VoxelLandChunkKey.h"
#include "Data/Caves/Field/VoxelLandCaveChunkFootprintRecord.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

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
	return TryBuildExpanded(Key, VoxelSizeCm, bSmooth, bNormalizeSlope, 0, 0, Out);
}

bool VoxelLand::M5::ChunkFootprint::TryBuildExpanded(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm, bool bSmooth,
	bool bNormalizeSlope, int32 MaskHalo, int32 OwnerHalo, FVoxelLandM5ChunkFootprint& Out)
{
	if (Key.LOD != 0 || VoxelSizeCm <= 0 || MaskHalo < 0 || OwnerHalo < 0)
	{
		return false;
	}
	const int64 Stored = FVoxelLandM5ChunkFootprintInternals::StoredPadding;
	const int64 Owner = Stored + int64(OwnerHalo);
	// Owner-expanded output samples each have their own mask neighborhood.
	// Both input radii are nonnegative int32, so the int64 sum cannot overflow.
	const int64 Baseline = Owner + int64(MaskHalo);
	const int64 Density = Baseline + (bSmooth ? FVoxelLandM5ChunkFootprintInternals::SmoothHalo : 0);
	const int64 Height = Density + (bNormalizeSlope ? FVoxelLandM5ChunkFootprintInternals::SlopeStencil : 0);
	FVoxelLandM5ChunkFootprint Result;
	if (!FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Stored, Result.StoredMin.X, Result.StoredMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Stored, Result.StoredMin.Y, Result.StoredMax.Y)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Z, VoxelSizeCm, Stored, Result.StoredMin.Z, Result.StoredMax.Z)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Baseline, Result.BaselineMin.X, Result.BaselineMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Baseline, Result.BaselineMin.Y, Result.BaselineMax.Y)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Z, VoxelSizeCm, Baseline, Result.BaselineMin.Z, Result.BaselineMax.Z)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.X, VoxelSizeCm, Owner, Result.OwnerMin.X, Result.OwnerMax.X)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Y, VoxelSizeCm, Owner, Result.OwnerMin.Y, Result.OwnerMax.Y)
		|| !FVoxelLandM5ChunkFootprintInternals::Axis(
			Key.Coordinate.Z, VoxelSizeCm, Owner, Result.OwnerMin.Z, Result.OwnerMax.Z)
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
