#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5PlanningMode.h"
#include "Data/Structs/VoxelLandM5Bounds.h"
#include "Data/Structs/VoxelLandM5CapsuleProfile.h"
#include "Data/Structs/VoxelLandM5TopologySettings.h"
#include "Data/Structs/VoxelLandM5NoisePocketSettings.h"
#include "Data/Structs/VoxelLandM5ProtectionSettings.h"
#include "Data/Structs/VoxelLandM5WaterSettings.h"
#include "Data/Structs/VoxelLandM5ResourceLimits.h"
#include "VoxelLandM5CaveSettings.generated.h"

// Configuration values only: no generation is enabled by adding this type.
// A stable stage ID must be explicitly assigned before an enabled stage is accepted.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5CaveSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Caves", meta = (ToolTip = "Enable the planned cave network."))
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, Category = "Caves", meta = (ToolTip = "Persistent stable identifier for this cave stage."))
	FGuid StageId;
	UPROPERTY(EditAnywhere, Category = "Caves", meta = (ToolTip = "Independent seed salt for the planned cave stage."))
	int64 Salt = 0;
	UPROPERTY(
		EditAnywhere, Category = "Caves", meta = (ToolTip = "Finite world planning or bounded regional planning."))
	EVoxelLandM5PlanningMode PlanningMode = EVoxelLandM5PlanningMode::Finite;
	UPROPERTY(EditAnywhere, Category = "Caves",
		meta = (ToolTip = "Half-open logical planning bounds; finite mode uses the upstream world footprint."))
	FVoxelLandM5Bounds Bounds;
	UPROPERTY(EditAnywhere, Category = "Caves",
		meta = (ClampMin = "1", ToolTip = "Horizontal width of each regional plan in centimetres."))
	int64 RegionSizeCm = 204800;
	UPROPERTY(
		EditAnywhere, Category = "Caves", meta = (ToolTip = "Logical world X of regional grid origin in centimetres."))
	int64 RegionOriginXCm = 0;
	UPROPERTY(
		EditAnywhere, Category = "Caves", meta = (ToolTip = "Logical world Y of regional grid origin in centimetres."))
	int64 RegionOriginYCm = 0;
	UPROPERTY(EditAnywhere, Category = "Caves",
		meta = (ClampMin = "0", ToolTip = "Required solid roof outside recorded entrances in centimetres."))
	int32 RoofThicknessCm = 1000;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5CapsuleProfile Capsule;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5TopologySettings Topology;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5NoisePocketSettings Pockets;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5ProtectionSettings Protection;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5WaterSettings Water;
	UPROPERTY(EditAnywhere, Category = "Caves")
	FVoxelLandM5ResourceLimits Limits;
};
