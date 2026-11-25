#pragma once
#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
struct FVoxelLandM5CavePlan;
struct FVoxelLandM5PointCm;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5Budget;
namespace VoxelLand::M5::ChunkCarve::Private
{
bool TryCarveOwned(TConstArrayView<int16> Baseline, const FIntVector& Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork,
	const FVoxelLandM5CandidateResult& Candidate, const FVoxelLandM5Budget& SourceBudget,
	TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples);
}
