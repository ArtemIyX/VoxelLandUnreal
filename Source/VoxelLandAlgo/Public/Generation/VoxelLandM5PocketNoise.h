#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Evaluation-local, thread-confined cache. It is not a geometry or publication proof.
class VOXELLANDALGO_API FVoxelLandM5PocketNoiseCache final
{
public:
	~FVoxelLandM5PocketNoiseCache();
	FVoxelLandM5PocketNoiseCache(const FVoxelLandM5PocketNoiseCache&) = delete;
	FVoxelLandM5PocketNoiseCache& operator=(const FVoxelLandM5PocketNoiseCache&) = delete;

	// Bounds are inclusive sample coordinates. Oversized corner boxes are rejected.
	[[nodiscard]] static FVoxelLandM5Diagnostic Build(const FVoxelLandM5PointCm& MinCm,
		const FVoxelLandM5PointCm& MaxCmInclusive, int64 PeriodCm, int64 WorldSeed, const FGuid& StageId,
		uint64 Salt, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TUniquePtr<FVoxelLandM5PocketNoiseCache>& Out);
	[[nodiscard]] FVoxelLandM5Diagnostic Sample(int64 X, int64 Y, int64 Z,
		TFunctionRef<bool()> IsCancelled, int32& OutValue);

private:
	FVoxelLandM5PocketNoiseCache(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FVoxelLandM5PointCm MinPosition, MaxPosition;
	int64 MinCell[3] = {};
	int32 Extent[3] = {};
	int64 Period = 0, Seed = 0;
	FGuid Stage;
	uint64 SaltValue = 0;
	TArray<int32> Values;
	TArray<uint8> Present;
};

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
