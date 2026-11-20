#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
class FRHICommandListBase;

// Editor-only, shared by scene proxies and the game-thread preview. Contains no UObjects.
class VOXELLANDRENDER_API FVoxelLandPreviewUploadObservation final
{
public:
    explicit FVoxelLandPreviewUploadObservation(int32 InExpectedCount);
    ~FVoxelLandPreviewUploadObservation();

    static bool IsSupported();
    void RecordUpload(FRHICommandListBase& RHICmdList);
    bool IsComplete();

private:
    class FState;
    TUniquePtr<FState> State;
};
#endif
