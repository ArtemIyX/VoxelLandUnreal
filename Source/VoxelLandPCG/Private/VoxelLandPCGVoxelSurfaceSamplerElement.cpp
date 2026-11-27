#include "VoxelLandPCGVoxelSurfaceSamplerElement.h"

#include "VoxelLandPCGNodes.h"
#include "VoxelLandPCGTerrainData.h"
#include "VoxelLandPCGVoxelSurfaceSamplerJob.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "Async/Async.h"
#include "Data/PCGBasePointData.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FVoxelLandPCGVoxelSurfaceSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	FVoxelLandPCGVoxelSurfaceSamplerContext& State = static_cast<FVoxelLandPCGVoxelSurfaceSamplerContext&>(*Context);
	const UPCGVoxelTerrainData* Data = FVoxelLandPCGNodes::GetTerrain(State);
	const UVoxelLandPCGVoxelSurfaceSamplerSettings* Settings =
		State.GetInputSettings<UVoxelLandPCGVoxelSurfaceSamplerSettings>();
	if (!Data || !Settings)
	{
		return FVoxelLandPCGNodes::Fail(State, NSLOCTEXT("VoxelLandPCG", "MissingSurfaceInput",
												   "Voxel Surface Sampler requires a ready voxel terrain input."));
	}
	if (Data->IsCancelled())
	{
		return true;
	}
	if (!State.Job)
	{
		return !Launch(State, *Data, *Settings);
	}
	if (!State.Job->IsComplete())
	{
		return false;
	}
	if (State.Job->IsCancelled())
	{
		return true;
	}
	if (!State.Job->Succeeded())
	{
		return FVoxelLandPCGNodes::Fail(State,
			NSLOCTEXT("VoxelLandPCG", "SurfaceSampleFailed",
				"Voxel Surface Sampler needs ready terrain, valid spacing and slope, and a candidate count within the cell budget."));
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGPublishSurfacePoints);
	const TConstArrayView<FVoxelLandTerrainPlacementPoint> Samples = State.Job->GetSamples();
#if WITH_EDITOR
	Data->RecordCandidateDebug(Samples);
#endif
	UPCGBasePointData* Points = FVoxelLandPCGNodes::MakePoints(State, Samples.Num());
	FPCGPointTransform::ValueRange Transforms = Points->GetTransformValueRange();
	FPCGPointSeed::ValueRange Seeds = Points->GetSeedValueRange();
	const FVector Origin(Data->GetView().OriginLocation);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		Transforms[Index] = FTransform(Samples[Index].Position - Origin);
		Seeds[Index] = static_cast<int32>(Samples[Index].Seed);
	}
	if (!FVoxelLandPCGNodes::WriteSurfaceMetadata(*Points, Samples))
	{
		return FVoxelLandPCGNodes::Fail(
			State, NSLOCTEXT("VoxelLandPCG", "SurfaceMetadataFailed", "Cannot write voxel surface attributes."));
	}
	FVoxelLandPCGNodes::PublishPoints(State, *Points);
	return true;
}

void FVoxelLandPCGVoxelSurfaceSamplerElement::AbortInternal(FPCGContext* Context) const
{
	if (!Context)
	{
		return;
	}
	FVoxelLandPCGVoxelSurfaceSamplerContext& State = static_cast<FVoxelLandPCGVoxelSurfaceSamplerContext&>(*Context);
	if (State.Job)
	{
		State.Job->Cancel();
	}
}

bool FVoxelLandPCGVoxelSurfaceSamplerElement::Launch(FVoxelLandPCGVoxelSurfaceSamplerContext& Context,
	const UPCGVoxelTerrainData& Terrain, const UVoxelLandPCGVoxelSurfaceSamplerSettings& Settings)
{
	if (!Terrain.GetWorkFence()->TryBeginWorker())
	{
		return false;
	}
	const TSharedRef<FVoxelLandPCGVoxelSurfaceSamplerJob, ESPMode::ThreadSafe> Job =
		MakeShared<FVoxelLandPCGVoxelSurfaceSamplerJob, ESPMode::ThreadSafe>(Terrain, Settings);
	Context.Job = Job;
	Context.bIsPaused = true;
	const TWeakPtr<FPCGContextHandle> Handle = Context.GetOrCreateHandle();
	Async(EAsyncExecution::ThreadPool, [Job, Handle] {
		Job->Run();
		AsyncTask(ENamedThreads::GameThread, [Handle] { Resume(Handle); });
	});
	return true;
}

void FVoxelLandPCGVoxelSurfaceSamplerElement::Resume(const TWeakPtr<FPCGContextHandle>& Handle)
{
	check(IsInGameThread());
	const FPCGContext::FSharedContext<FPCGContext> SharedContext(Handle);
	if (FPCGContext* Context = SharedContext.Get())
	{
		Context->bIsPaused = false;
	}
}
