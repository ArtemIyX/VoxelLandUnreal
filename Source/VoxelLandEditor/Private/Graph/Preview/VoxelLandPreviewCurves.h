#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4WorldSnapshot;

namespace VoxelLandM4PreviewCurves
{
// Bake all three captured authoring curves into this private worker snapshot.
// No asset is modified. Cancellation/failure leaves the snapshot unchanged.
bool Bake(FVoxelLandM4WorldSnapshot& Snapshot, TFunctionRef<bool()> IsCancelled, FString& Error);
}
