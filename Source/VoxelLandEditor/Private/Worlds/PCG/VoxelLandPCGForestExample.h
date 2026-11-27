#pragma once

#include "CoreMinimal.h"

class UPCGGraph;
class UVoxelLandPCGComponent;
class UVoxelLandTerrainGraph;

// Shared setup keeps the editable example and its verification on the same graph path.
class FVoxelLandPCGForestExample final
{
public:
	static FGuid BuildMask(UVoxelLandTerrainGraph& Graph);
	static bool BuildGraph(UPCGGraph& Graph, FGuid MaskId);
	static bool ConfigureLayer(UVoxelLandPCGComponent& Placement, UPCGGraph& Graph);
	static bool WriteAssets(FString& OutError);
};
