#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM5TopologySettings.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5TopologySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "1", ToolTip = "Minimum planned chambers in a planning domain."))
	int32 MinChambers = 8;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "1", ToolTip = "Maximum planned chambers in a planning domain."))
	int32 MaxChambers = 24;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "0", ToolTip = "Number of surface entrances requested for a planning domain."))
	int32 EntranceCount = 2;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "0", ToolTip = "Minimum number of additional loop connections."))
	int32 MinLoops = 0;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "0", ToolTip = "Maximum number of additional loop connections."))
	int32 MaxLoops = 3;
	UPROPERTY(
		EditAnywhere, Category = "Topology", meta = (ClampMin = "1", ToolTip = "Minimum tunnel radius in centimetres."))
	int32 MinTunnelRadiusCm = 250;
	UPROPERTY(
		EditAnywhere, Category = "Topology", meta = (ClampMin = "1", ToolTip = "Maximum tunnel radius in centimetres."))
	int32 MaxTunnelRadiusCm = 400;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "1", ToolTip = "Minimum chamber radius in centimetres."))
	int32 MinChamberRadiusCm = 500;
	UPROPERTY(EditAnywhere, Category = "Topology",
		meta = (ClampMin = "1", ToolTip = "Maximum chamber radius in centimetres."))
	int32 MaxChamberRadiusCm = 1000;
};
