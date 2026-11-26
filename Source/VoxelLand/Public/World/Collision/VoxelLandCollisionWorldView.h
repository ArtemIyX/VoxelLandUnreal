#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

class FVoxelLandCollisionField;
class AVoxelLandWorld;

// Immutable field refs and the Unreal coordinate frame from which they were gathered.
struct FVoxelLandCollisionWorldView
{
	TSharedPtr<const FVoxelLandCollisionField, ESPMode::ThreadSafe> Field;
	FIntVector OriginLocation = FIntVector::ZeroValue;
	uint64 GetConfigurationEpoch() const { return ConfigurationEpoch; }

private:
	friend class AVoxelLandWorld;
	FObjectKey WorldKey;
	uint64 ConfigurationEpoch = 0;
};
