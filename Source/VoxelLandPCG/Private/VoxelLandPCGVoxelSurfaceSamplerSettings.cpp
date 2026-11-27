#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "VoxelLandPCGVoxelSurfaceSamplerElement.h"
#include "VoxelLandPCGNodes.h"

TArray<FPCGPinProperties> UVoxelLandPCGVoxelSurfaceSamplerSettings::InputPinProperties() const
{
	return {FVoxelLandPCGNodes::TerrainPin(true)};
}

TArray<FPCGPinProperties> UVoxelLandPCGVoxelSurfaceSamplerSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UVoxelLandPCGVoxelSurfaceSamplerSettings::CreateElement() const
{
	return MakeShared<FVoxelLandPCGVoxelSurfaceSamplerElement>();
}
