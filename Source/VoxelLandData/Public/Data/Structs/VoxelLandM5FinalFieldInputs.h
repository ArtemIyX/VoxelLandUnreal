#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5NoisePocketSettings.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CavePlan;

// Borrowed, immutable, X-fastest inputs for one synchronous evaluation.
// The caller retains every referenced allocation throughout Build.
struct VOXELLANDDATA_API FVoxelLandM5FinalFieldInputs
{
	FVoxelLandM5FinalFieldInputs();
	FIntVector Size = FIntVector::ZeroValue;
	FVoxelLandM5PointCm OriginCm;
	int64 SpacingCm = 0;
	int32 VoxelSizeCm = 0;
	TConstArrayView<int16> Baseline;
	bool bNetworkEnabled = false;
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	TConstArrayView<uint8> NetworkAllowed;
	TConstArrayView<uint8> PocketAllowed;
	TConstArrayView<uint8> Protected;
	FVoxelLandM5NoisePocketSettings Pockets;
	int32 PocketNoiseVersion = 1;
	int32 CompositionVersion = 2;
	int32 GeometryVersion = 1;
	int64 WorldSeed = 0;
	FGuid StageId;
};
