#pragma once
#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"

// Call only after node, pin and link integrity has been validated.
class VOXELLANDDATA_API FVoxelLandGraphCycle
{
public:
    static bool Find(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes, TConstArrayView<FVoxelLandGraphLinkRecord> Links,
        FGuid& OutNode, FGuid& OutPin);
};
