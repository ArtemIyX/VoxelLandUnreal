#include "Rendering/Diagnostics/VoxelLandPreviewUploadObservation.h"

#if WITH_EDITOR
#include "DynamicRHI.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"
#include "RHICommandList.h"
#include "RHIResources.h"

class FVoxelLandPreviewUploadObservation::FState
{
public:
	explicit FState(int32 InExpectedCount)
		: ExpectedCount(InExpectedCount)
	{
	}

	FCriticalSection Mutex;
	TArray<FGPUFenceRHIRef> Fences;
	TArray<FRHIGPUMask> Masks;
	const int32 ExpectedCount;
};

FVoxelLandPreviewUploadObservation::FVoxelLandPreviewUploadObservation(int32 InExpectedCount)
	: State(MakeUnique<FState>(InExpectedCount))
{
	check(InExpectedCount > 0);
}

FVoxelLandPreviewUploadObservation::~FVoxelLandPreviewUploadObservation() = default;

bool FVoxelLandPreviewUploadObservation::IsSupported()
{
	return GDynamicRHI && !GUsingNullRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12;
}

void FVoxelLandPreviewUploadObservation::RecordUpload(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	FRHICommandListScopedPipelineGuard PipelineGuard(RHICmdList);
	if (RHICmdList.GetPipeline() != ERHIPipeline::Graphics)
	{
		return;
	}
	FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("VoxelLandPreviewUpload"));
	if (!Fence)
	{
		return;
	}
	const FRHIGPUMask Mask = RHICmdList.GetGPUMask();
	GDynamicRHI->RHIWriteGPUFence_TopOfPipe(RHICmdList, Fence);
	// The D3D12 write command holds a raw fence pointer until RHI translation.
	// Retain it even if the preview is cancelled before that command executes.
	RHICmdList.EnqueueLambda([Fence](FRHICommandListBase&) {});
	FScopeLock Lock(&State->Mutex);
	if (State->Fences.Num() < State->ExpectedCount)
	{
		State->Fences.Add(MoveTemp(Fence));
		State->Masks.Add(Mask);
	}
}

bool FVoxelLandPreviewUploadObservation::IsComplete()
{
	FScopeLock Lock(&State->Mutex);
	if (State->Fences.Num() != State->ExpectedCount)
	{
		return false;
	}
	for (int32 Index = 0; Index < State->Fences.Num(); ++Index)
	{
		if (!State->Fences[Index]->Poll(State->Masks[Index]))
		{
			return false;
		}
	}
	return true;
}
#endif