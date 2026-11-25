#pragma once

#include "CoreMinimal.h"

// Reserved independently of the built-in opcode range and external provider IDs.
inline FGuid VoxelLandGraphFunctionCallId()
{
	return FGuid(0x564c4d33, 1, 0, 0x10000);
}
