#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/VoxelLandWorldDefinition.h"
#include "Data/Structs/VoxelLandFiniteWorldSettings.h"
#include "VoxelLandFiniteWorld.generated.h"

struct FVoxelLandFiniteWorldSnapshot;

UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandFiniteWorld : public UVoxelLandWorldDefinition
{
	GENERATED_BODY()

public:
	virtual EVoxelLandWorldKind GetWorldKind() const override { return EVoxelLandWorldKind::Finite; }
	// Game thread only. Output is unchanged if authoring capture fails.
	bool CaptureSnapshot(FVoxelLandFiniteWorldSnapshot& OutSnapshot, FString& OutError) const;

	UPROPERTY(
		EditAnywhere, Category = "VoxelLand", meta = (ToolTip = "Bounded world grid and global erosion settings."))
	FVoxelLandFiniteWorldSettings Finite;
};
