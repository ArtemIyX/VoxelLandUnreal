#pragma once

#include "CoreMinimal.h"

// Explicit anchor role for a route endpoint. None is never a validated route anchor.
enum class EVoxelLandM5AnchorRole : uint8
{
	None,
	ChamberFeet,
	EntranceInsideFeet,
	EntranceOutsideFeet,
	PortalRegionAFeet,
	PortalRegionBFeet,
	JunctionFeet
};
