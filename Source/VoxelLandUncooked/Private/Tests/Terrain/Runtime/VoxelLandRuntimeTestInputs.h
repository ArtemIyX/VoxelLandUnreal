#pragma once

#include "CoreMinimal.h"

class UVoxelLandTerrainGraph;
class UVoxelLandWorldConfig;
struct FVoxelLandM4ControlConfig;

class FVoxelLandM4RuntimeTestInputs final
{
public:
	static bool MakeProgram(UVoxelLandTerrainGraph& Graph, FString& Error);
	static void BindAuthoredHeight(UVoxelLandWorldConfig& Config, UVoxelLandTerrainGraph& Graph);
	static void SetCurve(FVoxelLandM4ControlConfig& Channel);
};
