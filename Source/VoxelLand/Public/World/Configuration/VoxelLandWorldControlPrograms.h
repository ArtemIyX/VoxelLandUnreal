#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandProgram;
struct FVoxelLandWorldSourceSnapshot;

// Retains one captured world and its three validated built-in programs. This
// is an intermediate product, not a generator or a field/route certificate.
class VOXELLAND_API FVoxelLandWorldControlPrograms final
{
public:
	FVoxelLandWorldControlPrograms(const FVoxelLandWorldControlPrograms&) = delete;
	FVoxelLandWorldControlPrograms& operator=(const FVoxelLandWorldControlPrograms&) = delete;

	// Worker thread only. Caller must supply the source's admission lease from
	// the same budget, sized for its complete retained storage. This function
	// verifies provenance and the fixed record size, but cannot verify dynamic
	// storage. It retains the lease. No UObject access or recompilation occurs.
	// Providers, backend suitability, complete identity, plans and final field
	// remain separate requirements. Output is unchanged on failure.
	static FVoxelLandM5Diagnostic Load(TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Source,
		TSharedRef<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SourceLease,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandWorldControlPrograms, ESPMode::ThreadSafe>& OutPrograms);

	const FVoxelLandWorldSourceSnapshot& GetSource() const { return *Source; }
	const FVoxelLandProgram& GetContinental() const { return *Continental; }
	const FVoxelLandProgram& GetMountain() const { return *Mountain; }
	const FVoxelLandProgram& GetPeaks() const { return *Peaks; }

private:
	FVoxelLandWorldControlPrograms(TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> InSource,
		TSharedRef<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InSourceLease,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InContinental,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InMountain,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>&& InPeaks);

	TSharedRef<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SourceLease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Source;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Continental;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Mountain;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Peaks;
};
