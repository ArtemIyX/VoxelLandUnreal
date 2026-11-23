#pragma once

#include "CoreMinimal.h"

struct FCanonicalWriter;
struct FVoxelLandM5CaveSettings;

// Writes the fixed-width cave settings portion of a world descriptor.
// This function does not validate inputs or establish a world identity.
namespace VoxelLandM5CanonicalSettings
{
VOXELLAND_API void Write(FCanonicalWriter& Writer, const FVoxelLandM5CaveSettings& Settings);
}
