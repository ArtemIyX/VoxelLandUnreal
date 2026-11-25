#pragma once

#include "CoreMinimal.h"

class UVoxelLandTerrainGraph;
struct FVoxelLandM4ControlConfig;

class FVoxelLandM4RuntimeTestInputs final
{
public:
	static bool MakeProgram(UVoxelLandTerrainGraph& Graph, FString& Error);
	static void SetCurve(FVoxelLandM4ControlConfig& Channel);
};
