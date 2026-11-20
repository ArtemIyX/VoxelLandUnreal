#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphParameter.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "VoxelLandTerrainGraph.generated.h"

// Source records are data only. There is currently no compiler or executable payload.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandTerrainGraph : public UDataAsset
{
	GENERATED_BODY()

public:
	// Checks record integrity only; this does not establish that the graph can compile.
	bool ValidateStructure(FString& OutError) const;

	// Copies validated records on the game thread for subsequent worker use.
	bool CaptureSnapshot(FVoxelLandGraphSnapshot& OutSnapshot, FString& OutError) const;

	UPROPERTY(VisibleAnywhere, Category = "VoxelLand")
	int32 SourceVersion = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand")
	TArray<FVoxelLandGraphParameter> Parameters;

	UPROPERTY()
	TArray<FVoxelLandGraphNodeRecord> Nodes;

	UPROPERTY()
	TArray<FVoxelLandGraphLinkRecord> Links;
};
