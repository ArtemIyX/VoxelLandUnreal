#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5MultiRouteSupportSelection.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ScratchMesh;

// Candidate faces only. No support, clearance, source freshness, renderer,
// complete ownership, route safety or publication is established here.
class VOXELLANDALGO_API FVoxelLandM5SupportSelector final
{
public:
	// Finite direct RouteBuilder descendant of the exact composition source
	// retained by Mesh. Selects all potentially relevant upward faces, capped
	// at 256 per leg and MaxTotalFaces overall. Inputs remain immutable. The
	// caller must run the actual support and inflated obstacle checks; failure
	// or cancellation leaves Out unchanged.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CompositionSource,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed,
		TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh, int64 ContactSkinCm,
		int32 MaxTotalFaces, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe>& Out);
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> GetSelections() const { return Selections; }
private:
	FVoxelLandM5SupportSelector(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> InRouted,
		TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> InMesh)
		: Lease(MoveTemp(InLease)), Routed(MoveTemp(InRouted)), Mesh(MoveTemp(InMesh)) {}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
	TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
	TArray<FVoxelLandM5MultiRouteSupportSelection> Selections;
};
