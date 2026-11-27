#pragma once

#include "PCGContext.h"

class FVoxelLandPCGVoxelSurfaceSamplerJob;

struct FVoxelLandPCGVoxelSurfaceSamplerContext final : public FPCGContext
{
	~FVoxelLandPCGVoxelSurfaceSamplerContext() override;

	TSharedPtr<FVoxelLandPCGVoxelSurfaceSamplerJob, ESPMode::ThreadSafe> Job;
};
