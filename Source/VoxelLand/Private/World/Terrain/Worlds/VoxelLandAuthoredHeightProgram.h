#pragma once

#include "CoreMinimal.h"

class FVoxelLandGenerator;
class FVoxelLandProgram;
struct FVoxelLandM4WorldSnapshot;
struct FVoxelLandGeneratorSettings;

namespace VoxelLandAuthoredHeightProgram
{
bool CreateSource(const FVoxelLandM4WorldSnapshot& Snapshot, FVoxelLandGeneratorSettings& OutSettings,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutSource, FString& Error);
TArray<uint8> BuildPrepassKey(const FVoxelLandM4WorldSnapshot& Snapshot, const FVoxelLandProgram& Program);
}
