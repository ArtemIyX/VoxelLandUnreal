#pragma once

#include "VoxelLandM6SphereTestFixture.h"

#include "Data/Structs/VoxelLandM4SmoothSettings.h"

class FVoxelLandM7CheckpointTestFixture final
{
public:
	using FGridPtr = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;

	static constexpr int32 VoxelSizeCm = FVoxelLandM6SphereTestFixture::VoxelSizeCm;
	static constexpr int32 Cells = FVoxelLandChunkData::Cells;
	static constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;
	static constexpr uint8 CoarseLod = 1;
	static constexpr int32 CoarseStride = 2;
	static constexpr int32 LevelSpacingCm = VoxelSizeCm * CoarseStride;

	static bool NeverCancel() { return false; }

	static int32 Index(const FIntVector& Sample, int32 Axis = Samples)
	{
		return FVoxelLandM6SphereTestFixture::SampleIndex(Sample, Axis);
	}

	static bool CreateTiltedGrid(int32 OriginCm, int32 GridCells, int32 GridSpacing, FGridPtr& OutGrid)
	{
		TArray<int32> Heights;
		Heights.Reserve((GridCells + 1) * (GridCells + 1));
		for (int32 Y = 0; Y <= GridCells; ++Y)
		{
			for (int32 X = 0; X <= GridCells; ++X)
			{
				Heights.Add(HeightQ8(FInt64Vector(OriginCm + X * GridSpacing, OriginCm + Y * GridSpacing, 0)));
			}
		}
		return FVoxelLandM4HeightGrid::Create(OriginCm, OriginCm, GridCells, GridCells, GridSpacing, Heights, OutGrid);
	}

	static bool CreateSettings(FVoxelLandGeneratorSettings& OutSettings)
	{
		FGridPtr Unused;
		return FVoxelLandM6SphereTestFixture::CreateInputs(0, OutSettings, Unused);
	}

	static bool CreateGenerator(const FVoxelLandGeneratorSettings& Settings, const FGridPtr& Grid, bool bSmooth,
		bool bSlope, FGeneratorPtr& Out)
	{
		FVoxelLandM4SmoothSettings Smooth;
		Smooth.bEnabled = bSmooth;
		Smooth.Strength = bSmooth ? 0.5f : 0.f;
		return !FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, bSlope, {}, Smooth, {}, Out);
	}

	static int32 HeightQ8(const FInt64Vector& PointCm)
	{
		// h(x,y) = 23 cm + x/4 + y/8. Integer centimetre positions
		// admit all resulting fractions on the exact Q8 height lattice.
		return int32(23 * 256 + PointCm.X * 64 + PointCm.Y * 32);
	}

	static double HeightCm(const FVector& PointCm)
	{
		return 23.0 + PointCm.X / 4.0 + PointCm.Y / 8.0;
	}
};
