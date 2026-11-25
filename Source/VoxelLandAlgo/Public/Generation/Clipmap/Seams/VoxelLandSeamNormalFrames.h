#pragma once

#include "CoreMinimal.h"
#include "Data/Clipmap/Seams/VoxelLandSeamNormalAnchor.h"
#include "Data/Clipmap/Seams/VoxelLandSeamNormalFrame.h"

struct FVoxelLandMeshData;
struct FVoxelLandM7PairedNormalFrame;

class VOXELLANDALGO_API FVoxelLandM7NormalFrames final
{
public:
	static constexpr int32 MaxAnchorsPerBatch = 1024;
	static constexpr int32 MaxStencilCellsPerBatch = 256;
	static constexpr int32 StencilAxis = 4;
	static constexpr int32 StencilSamples = StencilAxis * StencilAxis * StencilAxis;

	// Input is global logical space, after undoing any render rebasing.
	static bool MakeAnchor(const FVector& LogicalCm, int64 BaseVoxelSizeCm, FVoxelLandM7NormalAnchor& OutAnchor);
	static bool IsCanonical(const FVoxelLandM7NormalAnchor& Anchor);
	// X-fastest completed field at offsets [-1,2] on all axes around Anchor.BaseCell.
	static bool Evaluate(
		TConstArrayView<int16> Stencil, const FVoxelLandM7NormalAnchor& Anchor, FVoxelLandM7NormalFrame& OutFrame);

	// Match the ordinary Surface Nets tie-break and renderer handedness.
	static FVector3f TangentForNormal(const FVector3f& Normal);
	static FVoxelLandM7NormalFrame BlendFromInterior(
		const FVoxelLandM7NormalFrame& Interior, const FVoxelLandM7NormalFrame& Shared, uint16 SharedWeight);
	// Each pair consumes one shared reference. Reject invalid or conflicting
	// indices before writing. The caller supplies the two terrain mesh streams.
	static bool ApplyPairedFrames(
		TConstArrayView<FVoxelLandM7PairedNormalFrame> Pairs, FVoxelLandMeshData& First, FVoxelLandMeshData& Second);

private:
	// The local mesh envelope stays inside 2^27 base cells. Logical doubles
	// retain at least 25 fractional bits here, comfortably finer than Q16.
	static constexpr int64 MaxBaseCell = int64(1) << 27;
	static constexpr int32 FractionOne = 1 << 16;
	static constexpr double GradientToleranceSquared = 1.e-12;

	static int16 Sample(TConstArrayView<int16> Stencil, const FIntVector& Cell);
	static FVector3f FirstCrossingDirection(TConstArrayView<int16> Stencil);
	static bool IsValidFrame(const FVoxelLandM7NormalFrame& Frame);
	static bool SameVectorBits(const FVector3f& First, const FVector3f& Second);
	static bool RecordFrame(TMap<int32, int32>& Targets, int32 Vertex, int32 PairIndex,
		TConstArrayView<FVoxelLandM7PairedNormalFrame> Pairs);
};
