#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;

// Immutable X-fastest output; the retained admission outlives all arrays.
class VOXELLANDALGO_API FVoxelLandM5CarveMasks final
{
public:
	FVoxelLandM5CarveMasks(const FVoxelLandM5CarveMasks&) = delete;
	FVoxelLandM5CarveMasks& operator=(const FVoxelLandM5CarveMasks&) = delete;

	TConstArrayView<uint8> GetNetworkAllowed() const { return NetworkAllowed; }
	TConstArrayView<uint8> GetPocketAllowed() const { return PocketAllowed; }
	TConstArrayView<uint8> GetProtected() const { return Protected; }

	// All arrays are immutable X-fastest sample grids. Start and Count select
	// the output inside Samples. The full mixed-cell search, including an extra
	// two-cell mesher margin, must fit: missing halo returns OutOfDomain.
	// Protection and EntranceAuthorized are binary sample-influence masks:
	// callers must conservatively mark every sample influencing protected solid
	// and certify every entrance-exempt sample's entire influence lies inside
	// its opening. Protection wins even inside an entrance. This function does
	// not certify those masks, baseline provenance, filter/mesher footprints,
	// water, a final field, or capsule routes. The caller must also run final
	// field safety validation after all contributions.
	//
	// bWaterPolicySatisfied means a separate water-policy certificate proves
	// that every output sample is eligible for both requested contributions.
	// This helper cannot express per-sample water restrictions; pass false and
	// fail closed if any such restriction or water information is unresolved.
	// Pocket depth uses a conservative vertical interval for all 5x5 adjacent
	// columns. Every such full input column must have a single enclosed solid to
	// air crossing with no zero density or later solid; otherwise it returns
	// Unproven. The caller certifies that each supplied column contains its
	// complete relevant vertical domain, and that no missing outside surface or
	// later stage invalidates this bound. Ambiguous overhangs/cavities are not
	// silently interpreted as a height surface. Depth is in centimetres.
	//
	// Baseline and masks must stay immutable throughout the call; inputs and
	// allocator bookkeeping are not charged to this budget. Failure or
	// cancellation leaves OutMasks unchanged.
	static FVoxelLandM5Diagnostic Build(FIntVector Size, const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm,
		FIntVector Start, FIntVector Count, TConstArrayView<int16> Baseline, TConstArrayView<uint8> Protection,
		TConstArrayView<uint8> EntranceAuthorized, int64 RoofThicknessCm, bool bPocketEnabled, int64 MinPocketDepthCm,
		int64 MaxPocketDepthCm, bool bWaterPolicySatisfied, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks);

private:
	explicit FVoxelLandM5CarveMasks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint8> NetworkAllowed;
	TArray<uint8> PocketAllowed;
	TArray<uint8> Protected;
};
