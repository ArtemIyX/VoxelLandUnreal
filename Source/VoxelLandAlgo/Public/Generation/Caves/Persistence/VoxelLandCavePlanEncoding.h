#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
struct FVoxelLandM5CavePlan;

// Canonical exact plan bytes, not a validation certificate. Arrays retain their
// supplied order and every field, including inactive payloads, is encoded.
namespace VoxelLand::M5::PlanEncoding
{
VOXELLANDALGO_API FVoxelLandM5Diagnostic Encode(const FVoxelLandM5CavePlan& Plan,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TArray<uint8>& OutBytes, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& OutLease);
}
