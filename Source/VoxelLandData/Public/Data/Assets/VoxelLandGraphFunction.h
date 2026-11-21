#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "VoxelLandGraphFunction.generated.h"

// Source-only function. Inherited graph parameters are its typed input interface.
UCLASS()
class VOXELLANDDATA_API UVoxelLandGraphFunction : public UVoxelLandTerrainGraph
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category = "Function")
    EVoxelLandGraphValueType OutputType = EVoxelLandGraphValueType::Float;

    UPROPERTY(VisibleAnywhere, Category = "Function")
    FGuid OutputNode;

    UPROPERTY(VisibleAnywhere, Category = "Function")
    FGuid OutputPin;
};
