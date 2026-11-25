#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5CapturedScratchChunks;

// Retains the source for successful local protection and roof sample checks.
// This is not opening authorization, complete influence, water or geometry
// safety, source freshness, a cave certificate, or publication permission.
class VOXELLAND_API FVoxelLandM5CapturedSolidChecks final
{
public:
	// Worker thread only. Uses the retained source's exact baseline, candidate,
	// mask, settings, manifest, seed and budget; failure preserves Out.
	static FVoxelLandM5Diagnostic Build(TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedSolidChecks, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5CapturedScratchChunks& GetSource() const { return *Source; }

private:
	FVoxelLandM5CapturedSolidChecks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> InSource)
		: Lease(MoveTemp(InLease))
		, Source(MoveTemp(InSource))
	{
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source;
};
