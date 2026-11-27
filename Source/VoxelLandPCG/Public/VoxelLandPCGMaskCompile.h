#pragma once

#include "CoreMinimal.h"

class FVoxelLandMaskGraphProgram;
class IVoxelLandMaterialSignalSource;

struct FVoxelLandPCGMaskCompile
{
	TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> Program;
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> Signals;
	FString Error;
};
