#include "VoxelLandPCGVoxelProjectSettings.h"
#include "VoxelLandPCGVoxelProjectElement.h"
#include "VoxelLandPCGNodes.h"

TArray<FPCGPinProperties> UVoxelLandPCGVoxelProjectSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = DefaultPointInputPinProperties();
	Pins.Add(FVoxelLandPCGNodes::TerrainPin(true));
	return Pins;
}

TArray<FPCGPinProperties> UVoxelLandPCGVoxelProjectSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UVoxelLandPCGVoxelProjectSettings::CreateElement() const
{
	return MakeShared<FVoxelLandPCGVoxelProjectElement>();
}
