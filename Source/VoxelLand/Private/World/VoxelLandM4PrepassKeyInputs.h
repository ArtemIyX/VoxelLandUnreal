#pragma once

#include "CoreTypes.h"

class FVoxelLandProgram;

struct FVoxelLandM4PrepassKeyInputs
{
	const FVoxelLandProgram& Continental;
	const FVoxelLandProgram& Mountain;
	const FVoxelLandProgram& Peaks;
	uint32 EncodingVersion;
	bool bAllowsUnlimited = false;
	bool bIsPrevalidated = false;
	int32 ReserveBytes = 0;
};
