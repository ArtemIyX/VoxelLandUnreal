#include "Generation/VoxelLandGenerator.h"

#include "Data/Structs/VoxelLandM7NormalFrameBatch.h"
#include "Generation/VoxelLandM7NormalFrames.h"

FVoxelLandError FVoxelLandGenerator::GenerateM7NormalFrames(TConstArrayView<FVoxelLandM7NormalAnchor> Anchors,
	FVoxelLandWorkerContext& Worker, FVoxelLandM7NormalFrameBatch& OutBatch, TFunctionRef<bool()> IsCancelled) const
{
	using FFrames = FVoxelLandM7NormalFrames;
	if (IsInGameThread())
	{
		return {EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Shared terrain normals require a worker thread")};
	}
	if (!OwnsWorkerOnCurrentThread(Worker))
	{
		return {EVoxelLandErrorCode::InvalidContext, TEXT("Shared normal worker belongs to a different generator or thread")};
	}
	if (!SupportsM7FiniteSource())
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("Shared terrain normals require ordinary finite M4 with M6 disabled")};
	}
	if (Anchors.Num() > FFrames::MaxAnchorsPerBatch)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Shared terrain normal batch exceeds the anchor limit")};
	}

	TMap<FInt64Vector, int32> Cells;
	Cells.Reserve(FMath::Min(Anchors.Num(), FFrames::MaxStencilCellsPerBatch));
	TArray<FInt64Vector> UniqueCells;
	UniqueCells.Reserve(FMath::Min(Anchors.Num(), FFrames::MaxStencilCellsPerBatch));
	TArray<int32> TileForAnchor;
	TileForAnchor.Reserve(Anchors.Num());
	for (const FVoxelLandM7NormalAnchor& Anchor : Anchors)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Shared terrain normal planning cancelled")};
		}
		if (!FFrames::IsCanonical(Anchor))
		{
			return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid canonical terrain normal anchor")};
		}
		if (const int32* Existing = Cells.Find(Anchor.BaseCell))
		{
			TileForAnchor.Add(*Existing);
			continue;
		}
		if (UniqueCells.Num() >= FFrames::MaxStencilCellsPerBatch)
		{
			return {EVoxelLandErrorCode::InvalidParameter, TEXT("Shared terrain normals exceed the unique stencil budget")};
		}
		if (const FVoxelLandError Error = ValidateM7FiniteBox(Anchor.BaseCell - FInt64Vector(1), FFrames::StencilAxis))
		{
			return Error;
		}
		const int32 Index = UniqueCells.Add(Anchor.BaseCell);
		Cells.Add(Anchor.BaseCell, Index);
		TileForAnchor.Add(Index);
	}

	TArray<TArray<int16>> Stencils;
	Stencils.SetNum(UniqueCells.Num());
	for (int32 Index = 0; Index < UniqueCells.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Shared terrain normal stencil reads cancelled")};
		}
		if (const FVoxelLandError Error = GenerateM4TiledBaselineBox(
				UniqueCells[Index] - FInt64Vector(1), FFrames::StencilAxis, Worker, Stencils[Index], IsCancelled))
		{
			return Error;
		}
	}

	FVoxelLandM7NormalFrameBatch Batch;
	Batch.Frames.Reserve(Anchors.Num());
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Shared terrain normal evaluation cancelled")};
		}
		FVoxelLandM7NormalFrame Frame;
		if (!FFrames::Evaluate(Stencils[TileForAnchor[Index]], Anchors[Index], Frame))
		{
			return {EVoxelLandErrorCode::InvalidContext, TEXT("Shared terrain normal stencil is incomplete")};
		}
		Batch.FallbackCount += uint32(Frame.bFallback);
		Batch.Frames.Add(Frame);
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Shared terrain normal publication cancelled")};
	}
	OutBatch = MoveTemp(Batch);
	return {};
}
