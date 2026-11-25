#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM5DefinitionInputs;
struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5ValidatedDefinition;

class VOXELLAND_API FVoxelLandM5DefinitionValidator final
{
public:
	// Worker thread; borrowed input is immutable throughout. Default bounds
	// require a complete typed finite XY declaration; usable chunks are derived later.
	// Failure and cancellation leave Out unchanged.
	static FVoxelLandM5Diagnostic Validate(const FVoxelLandM5DefinitionInputs& Inputs, FVoxelLandM5BudgetRef Budget,
		TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition>& Out);
#if WITH_DEV_AUTOMATION_TESTS
	// Tests the same presentation-only sanitization; cannot construct a definition.
	static void TestSanitizeTooltips(FVoxelLandM5WorldSnapshot& Snapshot);
#endif
};
