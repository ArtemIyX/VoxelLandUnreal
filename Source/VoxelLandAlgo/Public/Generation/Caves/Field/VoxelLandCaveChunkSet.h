#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandChunkData;
class FVoxelLandM5Budget;

namespace VoxelLand::M5::ChunkSet
{
// Check that every LOD-zero chunk in the half-open integer chunk-coordinate
// box is supplied exactly once, and that all three shared sample planes of
// each adjacent pair agree, including the halo intersections on both axes.
// Ordering of Chunks is irrelevant. No chunk is modified. Input arrays must
// remain immutable and their storage admitted by the caller for the call.
// MaxChunks is limited to 512; work is charged in advance to Budget.
//
// Success establishes only completeness and agreement of this explicitly
// supplied rectangle. It does not prove that the rectangle covers a route,
// that samples came from the matching final world, that neighboring mesh
// ownership is complete, or that any cave is safe or walkable.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckSampleAgreement(TConstArrayView<FVoxelLandChunkData> Chunks,
	const FIntVector& MinInclusive, const FIntVector& MaxExclusive, int32 MaxChunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
