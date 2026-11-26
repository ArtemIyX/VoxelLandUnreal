#pragma once

#include "CoreMinimal.h"
#include "Data/Assets/Worlds/VoxelLandWorldDefinition.h"
#include "Data/World/Configuration/VoxelLandFiniteWorldSettings.h"
#include "VoxelLandFiniteWorld.generated.h"

struct FVoxelLandFiniteWorldSnapshot;
class UVoxelLandM5ProtectionAsset;

UCLASS(Transient)
class VOXELLANDDATA_API UVoxelLandFiniteWorldSource : public UVoxelLandWorldSource
{
	GENERATED_BODY()

public:
	virtual EVoxelLandWorldKind GetWorldKind() const override { return EVoxelLandWorldKind::Finite; }
	// Game thread only. Output is unchanged if authoring capture fails.
	bool CaptureSnapshot(FVoxelLandFiniteWorldSnapshot& OutSnapshot, FString& OutError) const;

	UPROPERTY(
		EditAnywhere, Category = "VoxelLand", meta = (ToolTip = "Bounded world grid and global erosion settings."))
	FVoxelLandFiniteWorldSettings Finite;

	// Optional mask. An assigned asset always contributes protection; omission
	// is allowed only when the cave settings do not require a mask.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Caves")
	TObjectPtr<UVoxelLandM5ProtectionAsset> ProtectionMask = nullptr;
};
