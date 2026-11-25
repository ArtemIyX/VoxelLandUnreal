#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandGenerator;
struct FVoxelLandGeneratorSettings;
struct FVoxelLandM4LatticeGeometry;
struct FVoxelLandM4UnlimitedConfig;

namespace VoxelLandM4Unlimited
{
FVoxelLandError PrepareFiniteBaseline(const FVoxelLandGeneratorSettings& FinalSettings,
	const FVoxelLandM4UnlimitedConfig& Config, const FVoxelLandM4LatticeGeometry& Geometry,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, TFunctionRef<bool()> IsCancelled);
}
