#pragma once

#include "CoreMinimal.h"
#include "Graph/VoxelLandWorldCapabilitiesRequirement.h"

// Immutable capability declaration for one exact operator provider version.
// The requirement is a conservative maximum across every configuration of this
// exact provider version. Capture the declaration with the same frozen provider
// registry snapshot used by compile; unbound declarations cannot certify it.
struct VOXELLANDALGO_API FVoxelLandWorldOperatorCapability
{
	FGuid OperatorId;
	int32 OperatorVersion = 0;
	EVoxelLandWorldRequirement Requirement = EVoxelLandWorldRequirement::Unspecified;
};
