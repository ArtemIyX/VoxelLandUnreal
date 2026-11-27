#include "VoxelLandPCGVoxelMaskSampleSettings.h"
#include "VoxelLandPCGVoxelMaskSampleElement.h"
#include "VoxelLandPCGNodes.h"

TArray<FPCGPinProperties> UVoxelLandPCGVoxelMaskSampleSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = DefaultPointInputPinProperties();
	Pins.Add(FVoxelLandPCGNodes::TerrainPin(true));
	return Pins;
}

TArray<FPCGPinProperties> UVoxelLandPCGVoxelMaskSampleSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UVoxelLandPCGVoxelMaskSampleSettings::CreateElement() const
{
	return MakeShared<FVoxelLandPCGVoxelMaskSampleElement>();
}
