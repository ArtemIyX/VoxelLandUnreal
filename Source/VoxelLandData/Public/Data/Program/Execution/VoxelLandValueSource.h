#pragma once

#include "CoreMinimal.h"

// A value is supplied either by a constant or by a parameter of the stated type.
struct FVoxelLandValueSource
{
	bool bParameter = false;
	uint32 Index = 0;
};
