#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4SavedWorld;
struct FVoxelLandM4WorldSnapshot;
namespace VoxelLandM4SavedWorldPreflight
{
// Structural admission limits on an already-deserialized, trusted save. The
// caller must cap serialized input before deserialization. This is not exact
// allocator accounting and is not an untrusted archive parser.
// Bounds a trusted snapshot before it is copied, encoded or dispatched.
VOXELLAND_API bool ValidateSnapshot(const FVoxelLandM4WorldSnapshot& Snapshot, FString& Error);
// Also bounds the saved record envelope and identity bytes.
VOXELLAND_API bool Validate(const FVoxelLandM4SavedWorld& Saved, FString& Error);
}
