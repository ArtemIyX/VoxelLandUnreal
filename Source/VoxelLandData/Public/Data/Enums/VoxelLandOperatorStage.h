#pragma once
#include "CoreMinimal.h"
#include "VoxelLandOperatorStage.generated.h"

UENUM()
enum class EVoxelLandOperatorStage : uint8
{
    Sample,
    OneCellFilter
};
