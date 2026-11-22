#pragma once

#include "CoreMinimal.h"

// Feature namespaces are distinct, even if their numeric IDs coincide.
enum class EVoxelLandM5RouteFeatureKind : uint8
{
	Chamber,
	Connection,
	Entrance,
	Portal,
	Segment,
	Junction
};
