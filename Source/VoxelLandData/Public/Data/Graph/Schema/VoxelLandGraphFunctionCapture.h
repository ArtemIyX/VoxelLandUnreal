#pragma once

#include "CoreMinimal.h"

struct FVoxelLandGraphSnapshot;
struct FVoxelLandGraphDiagnostic;

// Resolves dependencies on the game thread and captures value records for workers.
class VOXELLANDDATA_API FVoxelLandGraphFunctionCapture
{
public:
	static bool Capture(FVoxelLandGraphSnapshot& Snapshot, FVoxelLandGraphDiagnostic& Diagnostic);
};
