#include "VoxelLandPCGGetVoxelTerrainSettings.h"
#include "VoxelLandPCGGetVoxelTerrainElement.h"
#include "VoxelLandPCGNodes.h"

TArray<FPCGPinProperties> UVoxelLandPCGGetVoxelTerrainSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UVoxelLandPCGGetVoxelTerrainSettings::OutputPinProperties() const
{
	return {FVoxelLandPCGNodes::TerrainPin(false)};
}

FPCGElementPtr UVoxelLandPCGGetVoxelTerrainSettings::CreateElement() const
{
	return MakeShared<FVoxelLandPCGGetVoxelTerrainElement>();
}
