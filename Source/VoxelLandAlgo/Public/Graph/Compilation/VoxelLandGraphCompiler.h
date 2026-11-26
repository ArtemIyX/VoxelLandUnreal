#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Graph/Schema/VoxelLandGraphDiagnostic.h"
#include "Data/Program/Execution/VoxelLandProgramDraft.h"
#include "Data/World/Configuration/VoxelLandWorldKind.h"
#include "Graph/Capabilities/VoxelLandWorldCapabilitiesDiagnostic.h"
#include "Graph/Compilation/VoxelLandGraphNodeOrigin.h"
#include "Templates/Function.h"

class FVoxelLandOperatorRegistrySnapshot;
class FVoxelLandM5Budget;

// Compiles a copied source snapshot; failure always clears OutDraft.
class VOXELLANDALGO_API FVoxelLandGraphCompiler
{
public:
	static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft, FString& OutError);
	static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandOperatorRegistrySnapshot& Providers,
		FVoxelLandProgramDraft& OutDraft, FString& OutError);
	static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft,
		FVoxelLandGraphDiagnostic& OutDiagnostic);
	static bool Compile(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandOperatorRegistrySnapshot& Providers,
		FVoxelLandProgramDraft& OutDraft, FVoxelLandGraphDiagnostic& OutDiagnostic);

	static bool CompileWithInstructionOrigins(const FVoxelLandGraphSnapshot& Snapshot, FVoxelLandProgramDraft& OutDraft,
		TArray<FVoxelLandGraphNodeOrigin>& OutOrigins, FVoxelLandGraphDiagnostic& OutDiagnostic);

	// World-directed compilation must use the same immutable source and frozen
	// provider snapshot for both capability validation and compilation. The
	// capability check and provider collection are budgeted and cancellable;
	// the existing compiler/flattening pass is not yet metered and is checked
	// for cancellation before and after execution. Failure preserves OutDraft.
	static bool CompileForWorld(const FVoxelLandGraphSnapshot& Snapshot, EVoxelLandWorldKind Kind,
		const FVoxelLandOperatorRegistrySnapshot& Providers, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, FVoxelLandProgramDraft& OutDraft,
		FVoxelLandWorldCapabilityDiagnostic& OutCapabilityDiagnostic, FVoxelLandGraphDiagnostic& OutDiagnostic);
};
