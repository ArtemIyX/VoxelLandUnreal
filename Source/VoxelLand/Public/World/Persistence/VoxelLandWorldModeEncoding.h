#pragma once

#include "CoreMinimal.h"

struct FCanonicalWriter;
struct FVoxelLandWorldSourceSnapshot;

// Encodes mode-specific, common generation and derived cave settings. The
// caller validates inputs and meters the writer. This is not a descriptor:
// graph, program and provider identity are absent. Presentation and streaming
// radii are deliberately excluded from generation identity.
namespace VoxelLandWorldModeEncoding
{
VOXELLAND_API void Write(FCanonicalWriter& Writer, const FVoxelLandWorldSourceSnapshot& Source);
}
