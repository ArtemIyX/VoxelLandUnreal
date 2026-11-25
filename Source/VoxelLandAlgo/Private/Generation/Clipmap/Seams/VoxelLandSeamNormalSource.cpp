#include "Generation/Field/VoxelLandGenerator.h"

#include "Data/Clipmap/Seams/VoxelLandSeamNormalFrameBatch.h"
#include "../Sampling/VoxelLandCompletedBox.h"
#include "../Sampling/VoxelLandLocalTerrainHeightSource.h"
#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"

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
		return {EVoxelLandErrorCode::InvalidContext,
			TEXT("Shared normal worker belongs to a different generator or thread")};
	}
	if (!SupportsM7CompletedField())
	{
		return {
			EVoxelLandErrorCode::UnsupportedLod, TEXT("Shared terrain normals require ordinary finite or local M4")};
	}
	if (Anchors.Num() > FFrames::MaxAnchorsPerBatch)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Shared terrain normal batch exceeds the anchor limit")};
	}

	TMap<FInt64Vector, int32> Cells;
	Cells.Reserve(FMath::Min(Anchors.Num(), FFrames::MaxStencilCellsPerBatch));
	TArray<FVoxelLandM7CompletedBox> UniqueCells;
	UniqueCells.Reserve(FMath::Min(Anchors.Num(), FFrames::MaxStencilCellsPerBatch));
	constexpr int64 MaxNormalWork = 16 * 1024 * 1024;
	int64 Work = 0;
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
			return {
				EVoxelLandErrorCode::InvalidParameter, TEXT("Shared terrain normals exceed the unique stencil budget")};
		}
		FVoxelLandM7CompletedBox Box;
		if (const FVoxelLandError Error =
				PlanM7CompletedBox(Anchor.BaseCell - FInt64Vector(1), FFrames::StencilAxis, Box))
		{
			return Error;
		}
		Work += Box.Work;
		if (Work > MaxNormalWork)
		{
			return {EVoxelLandErrorCode::InvalidParameter,
				TEXT("Shared terrain normal sampling exceeds the field work budget")};
		}
		const int32 Index = UniqueCells.Add(Box);
		Cells.Add(Anchor.BaseCell, Index);
		TileForAnchor.Add(Index);
	}

	FVoxelLandM7LocalHeightSource LocalSource(M4Unlimited.Get());
	TFunctionRef<bool(int64, int64, int32&)> LocalHeight(LocalSource);
	TFunctionRef<bool(int64, int64, int32&)>* Height = M4Unlimited ? &LocalHeight : nullptr;
	TArray<TArray<int16>> Stencils;
	Stencils.SetNum(UniqueCells.Num());
	for (int32 Index = 0; Index < UniqueCells.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Shared terrain normal stencil reads cancelled")};
		}
		if (const FVoxelLandError Error =
				ReadM7CompletedBox(UniqueCells[Index], Worker, Stencils[Index], IsCancelled, Height))
		{
			return LocalSource.PreferSourceError(Error);
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
