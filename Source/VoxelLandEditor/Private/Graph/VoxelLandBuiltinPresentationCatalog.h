#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"

// Editor-only text and style. Serialized operator contracts live in VoxelLandData.
class FVoxelLandBuiltinPresentationCatalog
{
public:
    static FText Title(EVoxelLandBuiltinNode Kind);
    static FText Description(EVoxelLandBuiltinNode Kind);
    static FText Category(EVoxelLandBuiltinNode Kind);
    static FLinearColor TitleColor(EVoxelLandBuiltinNode Kind);
};
