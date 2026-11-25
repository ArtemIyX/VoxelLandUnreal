#pragma once

#include "Containers/ArrayView.h"

struct FVoxelLandChunkKey;
struct FVoxelLandM7ClipmapLayout;
struct FVoxelLandM7ClipmapSettings;

// Computes aligned geometry demand only. Source and seam admission are performed
// by the caller before the desired layout replaces the visible layout.
class VOXELLANDALGO_API FVoxelLandM7Clipmap
{
public:
	static constexpr int32 MaxPlannerLeaves = 1024;

	// Preserve eligible prior splits before elective splits. Elective work leaves
	// a fallback reserve after admitting mandatory roots and retained coverage.
	[[nodiscard]] static bool Build(const FVoxelLandM7ClipmapSettings& Settings,
		const FVoxelLandM7ClipmapLayout* Previous, FVoxelLandM7ClipmapLayout& OutLayout);

	// Split each requested leaf and any neighbors required by 2:1 balance as one
	// transaction. Current must originate from Build or Refine with the same
	// settings. The output of either operation is unchanged on failure.
	[[nodiscard]] static bool Refine(const FVoxelLandM7ClipmapSettings& Settings, const FVoxelLandM7ClipmapLayout& Current,
		TConstArrayView<FVoxelLandChunkKey> RequiredLeaves, FVoxelLandM7ClipmapLayout& OutLayout);
};
