#pragma once

#include "CoreMinimal.h"

// Node domains are distinct even when their numeric IDs coincide.
enum class EVoxelLandM5EndpointKind : uint8
{
	Chamber,
	Entrance,
	Portal,
	Junction
};
