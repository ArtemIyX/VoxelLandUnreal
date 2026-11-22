#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandWorldKind.h"
#include "Graph/VoxelLandWorldCapabilitiesDiagnostic.h"
#include "Graph/VoxelLandWorldCapabilitiesOperator.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandGraphSnapshot;

// Domain validation for a captured graph and its complete function closure.
// ProviderCapabilities must be an immutable declaration captured with the exact
// provider registry snapshot later used by the compiler. Unknown providers fail
// closed. Each declaration covers the most demanding requirement across every
// configuration of that exact provider version; per-node configuration-specific
// resolution is not implemented. The caller owns input/provider memory and its admission. A successful
// result only establishes domain compatibility; compilation, provider validation,
// execution precision and actual regional plan connectivity are separate checks.
class VOXELLANDALGO_API FVoxelLandWorldCapabilities final
{
public:
	static FVoxelLandWorldCapabilityDiagnostic Validate(const FVoxelLandGraphSnapshot& Source, EVoxelLandWorldKind Kind,
		TConstArrayView<FVoxelLandWorldOperatorCapability> ProviderCapabilities,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
};
