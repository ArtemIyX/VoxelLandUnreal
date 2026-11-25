#pragma once
#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5FieldInputs;
class FVoxelLandWorkerContext;
class FVoxelLandM5Budget;
struct FVoxelLandChunkKey;
struct FVoxelLandChunkData;

namespace VoxelLand::M5::FinalField
{
// Scratch-only finite evaluation. An enabled network requires the exact sealed
// contributor inventory. No final-field or publication certificate.
VOXELLANDALGO_API FVoxelLandM5Diagnostic EvaluateChunk(const FVoxelLandM5FieldInputs& Inputs,
	const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk);
}
