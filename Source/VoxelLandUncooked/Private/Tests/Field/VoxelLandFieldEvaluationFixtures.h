#pragma once

#include "Generation/Field/VoxelLandGenerator.h"
#include "Data/Field/Diagnostics/VoxelLandFieldEvaluationFixtureResult.h"

namespace VoxelLandM1Fixtures
{
FVoxelLandInstruction Op(
	EVoxelLandOpcode Code, uint16 Dst = 0, uint16 A = 0, uint16 B = 0, uint16 C = 0, uint32 Imm = 0);
FVoxelLandProgramDraft Plane();
FVoxelLandProgramDraft Shapes();
FVoxelLandProgramDraft Noise(EVoxelLandOpcode NoiseOp);

FResult Generate(FVoxelLandProgramDraft Draft, FVoxelLandZoneProfile Profile, FVoxelLandChunkKey Key,
	FVoxelLandBackendOptions Options, uint64 Seed = 0);
FResult GenerateShared(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandChunkKey Key);
TArray<int16> PlaneReference(const FVoxelLandChunkKey& Key, int32 VoxelSizeCm);
uint64 ReferenceHash(const TArray<int16>& Density);
FString FirstDifference(const TArray<int16>& Expected, const TArray<int16>& Actual, const FVoxelLandChunkKey& Key);
}
