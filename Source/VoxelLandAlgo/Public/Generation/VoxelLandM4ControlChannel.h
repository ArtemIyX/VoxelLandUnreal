#pragma once

class FVoxelLandGenerator;
class FVoxelLandM4CurveLut;
class FVoxelLandWorkerContext;

struct FVoxelLandM4ControlChannel
{
	const FVoxelLandGenerator& Generator;
	FVoxelLandWorkerContext& Worker;
	const FVoxelLandM4CurveLut& Curve;
};
