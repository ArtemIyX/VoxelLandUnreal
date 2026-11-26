#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/Worlds/VoxelLandWorldDefinition.h"
#include "Data/World/Configuration/VoxelLandInfiniteWorldSettings.h"
#include "VoxelLandInfiniteWorld.generated.h"

struct FVoxelLandInfiniteWorldSnapshot;

UCLASS(Transient)
class VOXELLANDDATA_API UVoxelLandInfiniteWorldSource : public UVoxelLandWorldSource
{
	GENERATED_BODY()

public:
	virtual EVoxelLandWorldKind GetWorldKind() const override { return EVoxelLandWorldKind::Infinite; }
	// Game thread only. Output is unchanged if authoring capture fails.
	bool CaptureSnapshot(FVoxelLandInfiniteWorldSnapshot& OutSnapshot, FString& OutError) const;

	UPROPERTY(EditAnywhere, Category = "VoxelLand",
		meta = (ToolTip = "Local regional planning settings; no global prepass is used."))
	FVoxelLandInfiniteWorldSettings Infinite;
};
