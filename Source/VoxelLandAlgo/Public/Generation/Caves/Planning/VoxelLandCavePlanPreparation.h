#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;

// Scratch-only, worker-thread pipeline. A successful final mesh certificate is
// not implemented: this entry point always rejects before plan publication.
namespace VoxelLand::M5::PlanPreparation
{
// All callbacks are explicit trust boundaries. They must use the given budget
// for their complete native work and allocations and must not capture mutable
// UObjects. VerifyDescriptor must validate all current input and provider
// dependencies and compare complete canonical identity bytes, not just a hash.
// QuerySurface must satisfy EntrancePlanner's complete final-surface footprint
// contract. The future final certifier must inspect the actual final field/mesh,
// routes, floor, capsule, roof, protections, pockets and neighboring regions,
// returning Unproven whenever it cannot prove all of them; its output must be
// bound to the exact identity and plan. The current code never calls it.
// The caller owns provider/module leases, lifecycle and process admission.
// OutPlan is unchanged on every return; cancellation is checked at each stage.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TConstArrayView<uint8> ExpectedIdentity,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		const FVoxelLandM5CaveSettings&, int64, const FVoxelLandM5RegionCoord&, TConstArrayView<uint8>,
		TFunctionRef<bool()>)>
		VerifyDescriptor,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
		int64&, int64&, int64&, int32&, bool&, bool&)>
		QuerySurface,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		const FVoxelLandM5CaveSettings&, const FVoxelLandM5CavePlan&, TConstArrayView<uint8>, TFunctionRef<bool()>)>
		CertifyFinalField,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>& OutPlan);
}
