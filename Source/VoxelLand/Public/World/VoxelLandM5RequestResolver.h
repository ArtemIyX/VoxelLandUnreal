#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"

struct FVoxelLandChunkKey;
struct FVoxelLandM5Bounds;
struct FVoxelLandM5PointCm;
struct FVoxelLandM5WorldSnapshot;
class FVoxelLandM5ResolvedRequest;

// Does not validate providers or construct the conservative influence box.
// The caller supplies the full identity after current input/provider validation.
class VOXELLAND_API FVoxelLandM5RequestResolver final
{
public:
	static FVoxelLandM5Diagnostic Resolve(const FVoxelLandM5WorldSnapshot& Snapshot,
		const FVoxelLandM5PointCm& Location, TConstArrayView<uint8> ValidatedIdentity,
		FVoxelLandM5ResolvedRequest& OutRequest);

	// Influence is a previously validated conservative half-open box including
	// cave and pocket influence. Stored samples and one filter halo are checked.
	static FVoxelLandM5Diagnostic IntersectsStoredSamples(const FVoxelLandM5WorldSnapshot& Snapshot,
		const FVoxelLandM5Bounds& Influence, const FVoxelLandChunkKey& Key, bool& bOutIntersects);
};
