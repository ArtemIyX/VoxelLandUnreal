#pragma once

#include "Data/Structs/VoxelLandM4SmoothSettings.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"

class FVoxelLandM6SphereTestFixture final
{
public:
	static constexpr int32 VoxelSizeCm = 100;
	static constexpr int32 Cells = FVoxelLandChunkData::Cells;
	static constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;

	static bool NeverCancel() { return false; }
	static int32 SampleIndex(const FIntVector& Local, int32 Axis)
	{
		return Local.X + 1 + Axis * (Local.Y + 1 + Axis * (Local.Z + 1));
	}

	static bool CreateInputs(int32 HeightCm, FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& Grid)
	{
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadZ;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		Settings.Profile.VoxelSizeCm = VoxelSizeCm;
		constexpr int32 GridCellsX = 21;
		TArray<int32> Heights;
		Heights.Init(HeightCm * 256, (GridCellsX + 1) * 2);
		return !FVoxelLandProgram::Validate(Draft, Settings.Program)
			&& FVoxelLandM4HeightGrid::Create(-6400, -6400, GridCellsX, 1, 100000, Heights, Grid);
	}

	static FVoxelLandError CreateGenerator(const FVoxelLandGeneratorSettings& Settings,
		const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& Grid, bool bSmooth,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
	{
		FVoxelLandM4SmoothSettings Smooth;
		Smooth.bEnabled = bSmooth;
		Smooth.Strength = bSmooth ? 0.5f : 0.f;
		return FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, {}, Smooth, {}, OutGenerator);
	}

	static FVoxelLandM6SphereStamp Stamp(uint32 Id, EVoxelLandM6SphereOperation Operation,
		const FIntVector& CenterCm, int32 RadiusCm)
	{
		return {FGuid(0x4d36, 0, 0, Id), Operation, CenterCm, RadiusCm};
	}
};
