#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5CandidateResult;

// Immutable X-fastest sample exceptions for the existing carve-mask stage.
// This is NOT a final-field, surface, mesher, roof, protection or walkability
// certificate. The caller must verify the full M4/M5 descriptor and provider
// provenance, conservative protection, actual mesher footprint, all required
// neighboring plans, and the final field independently before publication.
class VOXELLANDALGO_API FVoxelLandM5EntranceMask final
{
public:
	FVoxelLandM5EntranceMask(const FVoxelLandM5EntranceMask&) = delete;
	FVoxelLandM5EntranceMask& operator=(const FVoxelLandM5EntranceMask&) = delete;
	TConstArrayView<uint8> GetAuthorized() const { return Authorized; }

	// Size/Origin describe a global X-fastest sample grid. Start/Count select
	// the output and must leave at least two samples of halo on all six sides.
	// Protection has Count.X*Count.Y*Count.Z binary entries in output order;
	// a protected sample never receives an entrance exception. The caller must
	// conservatively mark every sample whose influence touches protected solid.
	//
	// Each candidate is rechecked against Settings, seed, its signed plan region,
	// the same budget and the appropriate structural graph validator. Finite
	// plans must match the whole domain; regional plans must match their exact
	// aligned half-open grid region, and every portal must fit its shared face.
	// Voxel-size compatibility still requires world-identity validation. A sample
	// is authorized only when its complete closed +/- two-voxel influence box
	// lies within one recorded half-open entrance opening. Overlapping opening
	// boxes and duplicate regions are rejected as ambiguous; no union of partial
	// openings authorizes a sample. All influence boxes must lie in the logical
	// world bounds and +/-1048576 cm numeric envelope. This conservative box is
	// a prerequisite, not a substitute for the required final-field check.
	//
	// Inputs must be immutable, fully available and retained during the call;
	// their memory and allocator overhead are not charged here. Success retains
	// the exact plan references and a lease for output storage. Failure and
	// cancellation leave OutMask unchanged.
	static FVoxelLandM5Diagnostic Build(FIntVector Size, const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm,
		FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection, const FVoxelLandM5CaveSettings& Settings,
		int64 WorldSeed, TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask);

	static FVoxelLandM5Diagnostic BuildForJob(FIntVector Size, const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm,
		FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection, const FVoxelLandM5CaveSettings& Settings,
		int64 WorldSeed, TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask);
	// Only samples whose full influence fits the settings gate can be authorized.
	static FVoxelLandM5Diagnostic BuildGatedForJob(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
		int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection,
		const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
		TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask);

private:
	static FVoxelLandM5Diagnostic BuildImpl(FIntVector Size, const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm,
		FIntVector Start, FIntVector Count, TConstArrayView<uint8> Protection, const FVoxelLandM5CaveSettings& Settings,
		int64 WorldSeed, TConstArrayView<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Candidates,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe>& OutMask, bool bGated);
	explicit FVoxelLandM5EntranceMask(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
	TArray<uint8> Authorized;
};
