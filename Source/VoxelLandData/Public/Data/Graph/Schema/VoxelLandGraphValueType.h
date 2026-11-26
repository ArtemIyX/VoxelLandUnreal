#pragma once

#include "CoreMinimal.h"
#include "VoxelLandGraphValueType.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandGraphValueType : uint8
{
	Float,
	Int32,
	Bool,
	Surface
};
