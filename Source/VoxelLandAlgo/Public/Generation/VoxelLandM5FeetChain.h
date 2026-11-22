#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CapsuleProfile.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5Segment.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5FeetChainLeg.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

// An immutable, budget-admitted proposal for a single ordered connection.
// Segment and feet endpoints must meet exactly. This proves containment in
// each ideal tube only; it does not establish anchors, floor, final-field
// safety, junction coverage, or a complete route. Caller owns input storage.
class VOXELLANDALGO_API FVoxelLandM5FeetChain final
{
public:
	FVoxelLandM5FeetChain(const FVoxelLandM5FeetChain&) = delete;
	FVoxelLandM5FeetChain& operator=(const FVoxelLandM5FeetChain&) = delete;
	const TArray<FVoxelLandM5FeetChainLeg>& GetLegs() const { return Legs; }

	// Input order must be the intended traversal order. bReverseFirst selects
	// orientation of the first segment; subsequent orientation is derived.
	// Ids are plan-local; zero is valid. On failure Out remains unchanged.
	static FVoxelLandM5Diagnostic Build(TConstArrayView<FVoxelLandM5Segment> Segments, uint64 ConnectionId,
		bool bReverseFirst, const FVoxelLandM5CapsuleProfile& Capsule, int32 MaxSegments,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5FeetChain, ESPMode::ThreadSafe>& Out);

private:
	FVoxelLandM5FeetChain(
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease, TArray<FVoxelLandM5FeetChainLeg>&& InLegs)
		: Lease(MoveTemp(InLease))
		, Legs(MoveTemp(InLegs))
	{
	}
	// Members destroy in reverse declaration order: storage before admission.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<FVoxelLandM5FeetChainLeg> Legs;
};
