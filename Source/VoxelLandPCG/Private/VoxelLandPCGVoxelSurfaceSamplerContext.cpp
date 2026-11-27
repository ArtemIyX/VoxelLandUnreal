#include "VoxelLandPCGVoxelSurfaceSamplerContext.h"
#include "VoxelLandPCGVoxelSurfaceSamplerJob.h"

FVoxelLandPCGVoxelSurfaceSamplerContext::~FVoxelLandPCGVoxelSurfaceSamplerContext()
{
	if (Job)
	{
		Job->Cancel();
	}
}
