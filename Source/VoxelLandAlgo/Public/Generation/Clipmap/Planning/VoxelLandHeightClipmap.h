#pragma once

#include "Containers/Map.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"

struct FVoxelLandM7ClipmapLayout;
struct FVoxelLandM7ClipmapSettings;

/** Plans the XY surface of a single-valued height terrain, without spending leaves on empty vertical volume. */
class VOXELLANDALGO_API FVoxelLandHeightClipmap final
{
public:
	static constexpr uint8 NegativeX = 1;
	static constexpr uint8 PositiveX = 2;
	static constexpr uint8 NegativeY = 4;
	static constexpr uint8 PositiveY = 8;

	static double MaxDetailLookAheadCm(double HorizontalRadiusCm);

	/** The keys have Z=0. A coarse edge bit requests samples at the finer neighboring spacing. */
	[[nodiscard]] static bool Build(const FVoxelLandM7ClipmapSettings& Settings,
		const FVoxelLandM7ClipmapLayout* Previous, FVoxelLandM7ClipmapLayout& OutLayout,
		TMap<FVoxelLandChunkKey, uint8>& OutCoarseEdgeMasks);

private:
	using FKey = FVoxelLandChunkKey;
	using FPriority = TPair<double, FKey>;

	static constexpr int32 ChildrenPerSplit = 4;
	static constexpr int32 ExtraLeavesPerSplit = ChildrenPerSplit - 1;
	static constexpr int32 Edges = 4;

	static bool MakeRoots(FVoxelLandM7ClipmapLayout& Layout);
	static FKey ProbeCoarserNeighbor(const FKey& Key, int32 Edge);
	static double SquaredDistanceToTile(const FVector2D& Minimum, double Width, const FVector2D& Point);
	static FPriority MakePriority(const FKey& Key, const FVoxelLandM7ClipmapSettings& Settings);
	static void Split(
		const FKey& Key, FVoxelLandM7ClipmapLayout& Layout, TSet<FKey>& Leaves, TArray<FPriority>& Pending);
	static void WriteFrontier(
		const TSet<FKey>& Leaves, FVoxelLandM7ClipmapLayout& Layout, TMap<FKey, uint8>& CoarseEdgeMasks);
};
