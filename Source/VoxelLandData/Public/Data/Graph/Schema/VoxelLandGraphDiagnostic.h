#pragma once
#include "CoreMinimal.h"

// Optional node and pin identifiers refer only to existing, unambiguous records.
struct FVoxelLandGraphDiagnostic
{
	FString Message;
	FGuid NodeId;
	FGuid PinId;
};
