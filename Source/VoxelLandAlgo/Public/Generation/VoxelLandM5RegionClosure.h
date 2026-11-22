#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5RegionEdgeInput.h"
#include "Generation/VoxelLandM5RegionInput.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5CandidateResult;
class FVoxelLandM5RegionClosure;

namespace VoxelLand::M5::RegionClosure
{
// This is a bounded scratch-set consistency check, not a certificate of final
// field safety or permission to publish. The caller must provide the complete
// required region set for its sample/influence footprint, validated descriptor
// and immutable overlap data with adequate influence and mesher halo. Every
// face-adjacent pair in the set requires exactly one supplied overlap. Edges
// leaving this set are not certified; the caller must extend the set before
// relying on them. The required set must itself be connected.
//
// VerifyDescriptor is a trusted, budget-aware native callback that must fully
// validate the current descriptor/providers; it is invoked before checks and
// immediately before success. Inputs and callback storage remain caller-owned.
// On failure/cancellation OutClosure is unchanged. Work is not refunded.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TConstArrayView<FVoxelLandM5RegionCoord> Required, TConstArrayView<FVoxelLandM5RegionInput> Regions,
	TConstArrayView<FVoxelLandM5RegionEdgeInput> Edges, TConstArrayView<uint8> ExpectedDescriptor, int32 VoxelSizeCm,
	int32 HaloSamples, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TConstArrayView<uint8>, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>)>
		VerifyDescriptor,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe>& OutClosure);
}

// Retains the candidate plan leases and a copy of the descriptor. This object
// represents agreement of the supplied bounded set only; it does not certify
// the final field, an outside neighbor, or gameplay walkability.
class VOXELLANDALGO_API FVoxelLandM5RegionClosure final
{
public:
	FVoxelLandM5RegionClosure(const FVoxelLandM5RegionClosure&) = delete;
	FVoxelLandM5RegionClosure& operator=(const FVoxelLandM5RegionClosure&) = delete;
	TConstArrayView<FVoxelLandM5RegionCoord> GetRegions() const { return Regions; }
	TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> GetCandidates() const
	{
		return Candidates;
	}
	TConstArrayView<uint8> GetDescriptor() const { return Descriptor; }

private:
	friend FVoxelLandM5Diagnostic VoxelLand::M5::RegionClosure::Build(const FVoxelLandM5CaveSettings&, int64,
		TConstArrayView<FVoxelLandM5RegionCoord>, TConstArrayView<FVoxelLandM5RegionInput>,
		TConstArrayView<FVoxelLandM5RegionEdgeInput>, TConstArrayView<uint8>, int32, int32,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<FVoxelLandM5Diagnostic(
			TConstArrayView<uint8>, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>)>,
		TFunctionRef<bool()>, TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe>&);
	FVoxelLandM5RegionClosure(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease, TArray<FVoxelLandM5RegionCoord>&& InRegions,
		TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>>&& InCandidates,
		TArray<uint8>&& InDescriptor)
		: Budget(MoveTemp(InBudget))
		, Lease(MoveTemp(InLease))
		, Regions(MoveTemp(InRegions))
		, Candidates(MoveTemp(InCandidates))
		, Descriptor(MoveTemp(InDescriptor))
	{
	}
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<FVoxelLandM5RegionCoord> Regions;
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates;
	TArray<uint8> Descriptor;
};
