#include "VoxelLandPCGVoxelSurfaceSamplerJob.h"

#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "VoxelLandPCGTerrainData.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FVoxelLandPCGVoxelSurfaceSamplerJob::FVoxelLandPCGVoxelSurfaceSamplerJob(
	const UPCGVoxelTerrainData& Terrain, const UVoxelLandPCGVoxelSurfaceSamplerSettings& Settings)
	: WorkFence(Terrain.GetWorkFence())
	, Field(Terrain.GetView().Field)
	, Chunks(Terrain.GetChunks())
	, Bounds(Terrain.GetLogicalBounds())
	, MinimumSpacingCm(Settings.MinimumSpacingCm)
	, MaximumSlopeDegrees(Settings.MaximumSlopeDegrees)
	, Seed(static_cast<int32>(HashCombineFast(GetTypeHash(Terrain.GetMaskSeed()), GetTypeHash(Settings.GetSeed()))))
{
}

FVoxelLandPCGVoxelSurfaceSamplerJob::~FVoxelLandPCGVoxelSurfaceSamplerJob()
{
	if (bHasReservation)
	{
		ReleaseWorker();
	}
}

void FVoxelLandPCGVoxelSurfaceSamplerJob::Run()
{
	check(!IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGSurfaceWorker);
	if (Field && !IsCancelled())
	{
		bSucceeded = FVoxelLandTerrainPlacement::Sample(
			{*Field, Chunks, Bounds}, [this] { return IsCancelled(); }, MinimumSpacingCm, Seed, MaximumSlopeDegrees,
			Samples);
	}
	ReleaseWorker();
	bComplete.Store(true);
}

void FVoxelLandPCGVoxelSurfaceSamplerJob::ReleaseWorker()
{
	Chunks.Empty();
	Field.Reset();
	WorkFence->EndWorker();
	bHasReservation = false;
}
