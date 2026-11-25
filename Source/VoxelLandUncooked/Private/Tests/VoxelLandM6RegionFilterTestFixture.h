#pragma once

#include "VoxelLandM6SphereTestFixture.h"

class FVoxelLandM6RegionFilterTestFixture final
{
public:
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FGridPtr = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;
	using FFixture = FVoxelLandM6SphereTestFixture;

	static FVoxelLandM6FilterStage Stage(uint32 Id, EVoxelLandM6FilterKind Kind, const FInt64Vector& Min,
		const FInt64Vector& Max, uint16 Strength = MAX_uint16)
	{
		FVoxelLandM6FilterStage Result;
		Result.Id = FGuid(0x4d36, 3, 0, Id);
		Result.Kind = Kind;
		Result.MinCm = Min;
		Result.MaxCm = Max;
		Result.Strength = Strength;
		return Result;
	}

	static bool CreateFinite(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid,
		TConstArrayView<FVoxelLandM6FilterStage> Stages, bool bCompatibilitySmooth, FGeneratorPtr& OutGenerator)
	{
		Settings.M6Filters.Stages = Stages;
		return !FFixture::CreateGenerator(Settings, Grid, bCompatibilitySmooth, OutGenerator);
	}

	static bool Generate(const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& Key, FChunkPtr& OutChunk)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		return !Generator.CreateWorker(Worker) && !Generator.GenerateChunk(Key, *Worker, OutChunk) && OutChunk.IsValid();
	}
};
