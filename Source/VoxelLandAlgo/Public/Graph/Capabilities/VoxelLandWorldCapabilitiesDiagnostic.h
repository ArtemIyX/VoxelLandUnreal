#pragma once

#include "CoreMinimal.h"
#include "Graph/Capabilities/VoxelLandWorldCapabilitiesError.h"

// NodeId is the authored node responsible for the diagnostic when available;
// FunctionPath identifies its containing captured function, or is empty at root.
struct VOXELLANDALGO_API FVoxelLandWorldCapabilityDiagnostic
{
	EVoxelLandWorldCapabilityError Code = EVoxelLandWorldCapabilityError::None;
	FGuid NodeId;
	FString FunctionPath;
	FString Message;
};
