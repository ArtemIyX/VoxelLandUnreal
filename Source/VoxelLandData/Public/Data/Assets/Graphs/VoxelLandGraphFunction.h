#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "VoxelLandGraphFunction.generated.h"

// Source-only function. Inherited graph parameters are its typed input interface.
UCLASS(HideDropdown)
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
