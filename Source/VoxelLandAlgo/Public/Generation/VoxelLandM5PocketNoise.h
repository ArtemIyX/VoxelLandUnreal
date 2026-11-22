#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

// Version 1 deterministic global lattice field. Values are dimensionless
// signed integers in [-32768,32767], not a distance or a safety guarantee.
// This API does not perform carving, roof/protection checks or integration.
namespace VoxelLand::M5::PocketNoise
{
// Canonical 68-byte packet for a lattice corner. All int64 corner indices are
// representable in the encoding; sampling bounds are enforced by TrySample.
VOXELLANDALGO_API bool TryCornerPacket(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 CellX, int64 CellY,
	int64 CellZ, TArray<uint8>& OutPacket);

// Coordinates and period are whole centimetres. Sample coordinates must lie
// in [-1048576,1048576]; period must lie in [1,1048576]. On any failure or
// cancellation, OutValue is unchanged. No UObject access or mutable state.
VOXELLANDALGO_API bool TrySample(int64 X, int64 Y, int64 Z, int64 PeriodCm, int64 WorldSeed, const FGuid& StageId,
	uint64 Salt, TFunctionRef<bool()> IsCancelled, int32& OutValue);
}
