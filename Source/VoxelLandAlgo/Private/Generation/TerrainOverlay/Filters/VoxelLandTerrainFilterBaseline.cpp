#include "Generation/Field/VoxelLandGenerator.h"

#include "Data/Terrain/Domains/VoxelLandTerrainLatticeGeometry.h"
#include "Data/Clipmap/Sampling/VoxelLandTerrainSampleGrid.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "../../Terrain/Worlds/VoxelLandLocalBaseline.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandChunkLocalTerrain.h"
#include "../../Field/VoxelLandQuantizedDensity.h"

int32 FVoxelLandGenerator::GetM6CompatibilityHalo() const
{
	const FVoxelLandM4SmoothSettings* Override =
		M4Unlimited && M4Unlimited->SmoothOverride.IsSet() ? &M4Unlimited->SmoothOverride.GetValue() : nullptr;
	const bool bSmooth =
		Override ? Override->bEnabled && Override->Strength > 0.f : bSmoothEnabled && SmoothStrength > 0.f;
	return bSmooth ? 1 : 0;
}

FVoxelLandError FVoxelLandGenerator::ValidateM6BaselineFootprint(const FVoxelLandChunkKey& Key, int32 Halo) const
{
	if (Key.LOD != 0)
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("M6 pipeline requires LOD 0")};
	}
	if (Halo < 0 || Halo > FVoxelLandM6FilterSettings::MaxHalo || (!M4HeightGrid && !M4Unlimited))
	{
		return {EVoxelLandErrorCode::InvalidContext, TEXT("Invalid M6 baseline context")};
	}
	if (M4Unlimited)
	{
		if (const FVoxelLandError Error = VoxelLandM4Unlimited::ValidateChunkDemand(Key, Profile.VoxelSizeCm))
		{
			return Error;
		}
	}
	if (bM6LegacySphereEnvelope)
	{
		if (const FVoxelLandError Error = ValidateM6SphereFootprint(Key, Halo))
		{
			return Error;
		}
	}

	// The int32 key and this pipeline's bounded integer voxel size admit
	// every product in signed int64 before the source-domain checks.
	const int32 DensityMargin = Halo + GetM6CompatibilityHalo();
	const bool bSlope = M4Unlimited ? M4Unlimited->bNormalizeSlope : bM4NormalizeSlope;
	const int32 XYMargin = DensityMargin + int32(bSlope);
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector StoredMin = FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1);
	const FInt64Vector Margin(XYMargin, XYMargin, DensityMargin);
	const FInt64Vector MinCm = (StoredMin - Margin) * VoxelSizeCm;
	const FInt64Vector MaxCm =
		(StoredMin + Margin + FInt64Vector(FVoxelLandChunkData::SamplesPerAxis - 1)) * VoxelSizeCm;
	constexpr int64 MaxBaselineZCm = 2000000;
	if (MinCm.Z < -MaxBaselineZCm || MaxCm.Z > MaxBaselineZCm)
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M6 pipeline input Z exceeds the M4 fixed range")};
	}
	if (M4Unlimited)
	{
		return {};
	}

	const bool bNative = !bM4FixedLatticeShaping
					  && ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
						  || (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0));
	constexpr int64 MaxExactFloatCm = 16777216;
	if (bNative
		&& (MinCm.X < -MaxExactFloatCm || MaxCm.X > MaxExactFloatCm || MinCm.Y < -MaxExactFloatCm
			|| MaxCm.Y > MaxExactFloatCm))
	{
		return {
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M6 pipeline input exceeds the native exact XY lattice")};
	}
	int32 HeightQ8 = 0;
	if (!M4HeightGrid->Sample(MinCm.X, MinCm.Y, HeightQ8) || !M4HeightGrid->Sample(MaxCm.X, MaxCm.Y, HeightQ8))
	{
		return {EVoxelLandErrorCode::InvalidParameter,
			TEXT("M6 pipeline input including compatibility halo and slope stencil is outside the M4 grid")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM6BaselineBox(const FVoxelLandChunkKey& Key, int32 Halo,
	FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled) const
{
	const int32 Axis = FVoxelLandChunkData::SamplesPerAxis + 2 * Halo;
	const FInt64Vector MinLattice =
		FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1 + Halo);
	return GenerateM4TiledBaselineBox(MinLattice, Axis, Worker, Density, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::GenerateM4TiledBaselineBox(const FInt64Vector& MinLattice, int32 Axis,
	FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	check((M4HeightGrid || LocalHeight) && OwnsWorkerOnCurrentThread(Worker));
	// Both callers admit their complete box before tiling. The two-level
	// checkpoint is the larger allowed box: 35 outputs at stride two.
	constexpr int32 MaxTile = FVoxelLandChunkData::SamplesPerAxis;
	check(Axis >= 1 && Axis <= 2 * (MaxTile - 1) + 1);
	const int32 CompatibilityHalo = GetM6CompatibilityHalo();
	const int32 Strength = int32(double(SmoothStrength) * MAX_uint16 + 0.5);
	Density.SetNumUninitialized(Axis * Axis * Axis);
	for (int32 Z = 0; Z < Axis; Z += MaxTile)
	{
		for (int32 Y = 0; Y < Axis; Y += MaxTile)
		{
			for (int32 X = 0; X < Axis; X += MaxTile)
			{
				const FIntVector Tile(
					FMath::Min(MaxTile, Axis - X), FMath::Min(MaxTile, Axis - Y), FMath::Min(MaxTile, Axis - Z));
				const FIntVector RawSize = Tile + FIntVector(2 * CompatibilityHalo);
				const FVoxelLandM7SampleGrid RawGrid{
					MinLattice + FInt64Vector(X, Y, Z) - FInt64Vector(CompatibilityHalo), RawSize, 1};
				TArray<int16> Raw;
				if (const FVoxelLandError Error = GenerateM4SampleGrid(RawGrid, Worker, Raw, IsCancelled, LocalHeight))
				{
					return Error;
				}
				for (int32 K = 0; K < Tile.Z; ++K)
				{
					if (IsCancelled())
					{
						return {EVoxelLandErrorCode::Cancelled, TEXT("M6 baseline smoothing cancelled")};
					}
					for (int32 J = 0; J < Tile.Y; ++J)
					{
						for (int32 I = 0; I < Tile.X; ++I)
						{
							const int32 Offset = (X + I) + Axis * ((Y + J) + Axis * (Z + K));
							Density[Offset] = CompatibilityHalo ? FVoxelLandQuantizedDensity::SmoothSample(
																	  Raw, RawSize, I, J, K, Strength)
																: Raw[I + RawSize.X * (J + RawSize.Y * K)];
						}
					}
				}
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM6LocalBaselineBox(
	const FVoxelLandChunkKey& Key, int32 Halo, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled) const
{
	check(M4Unlimited && M4UnlimitedSettings);
	const int32 Margin = Halo + GetM6CompatibilityHalo() + int32(M4Unlimited->bNormalizeSlope);
	const int32 Cells = FVoxelLandChunkData::SamplesPerAxis - 1 + 2 * Margin;
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector OriginCm =
		(FInt64Vector(Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1 + Margin)) * VoxelSizeCm;
	const FVoxelLandM4LatticeGeometry Geometry{OriginCm.X, OriginCm.Y, VoxelSizeCm, Cells, Cells};
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Baseline;
	if (const FVoxelLandError Error = VoxelLandM4Unlimited::PrepareFiniteBaseline(
			*M4UnlimitedSettings, *M4Unlimited, Geometry, Baseline, IsCancelled))
	{
		return Error;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Baseline->CreateWorker(Worker))
	{
		return Error;
	}
	return Baseline->GenerateM6BaselineBox(Key, Halo, *Worker, Density, IsCancelled);
}
