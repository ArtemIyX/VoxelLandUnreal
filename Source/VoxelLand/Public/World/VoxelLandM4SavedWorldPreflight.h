#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4SavedWorld;
namespace VoxelLandM4SavedWorldPreflight
{
// Structural admission limits on an already-deserialized, trusted save. The
// caller must cap serialized input before deserialization. This is not exact
// allocator accounting and is not an untrusted archive parser.
VOXELLAND_API bool Validate(const FVoxelLandM4SavedWorld& Saved, FString& Error);
}
