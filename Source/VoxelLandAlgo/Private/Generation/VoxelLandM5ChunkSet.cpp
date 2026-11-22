#include "Generation/VoxelLandM5ChunkSet.h"

#include "Data/Structs/VoxelLandChunkData.h"
#include "Generation/VoxelLandM5Budget.h"

namespace
{
FVoxelLandM5Diagnostic M5ChunkSetError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Error;
	Error.Code = Code;
	Error.Stage = TEXT("M5.ChunkSet");
	Error.Message = Message;
	return Error;
}

int32 M5ChunkSetIndex(int32 X, int32 Y, int32 Z)
{
	return X + 35 * (Y + 35 * Z);
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::ChunkSet::CheckSampleAgreement(TConstArrayView<FVoxelLandChunkData> Chunks,
	const FIntVector& MinInclusive, const FIntVector& MaxExclusive, int32 MaxChunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
	}
	if (MaxChunks < 1 || MaxChunks > 512 || Chunks.Num() < 1 || Chunks.Num() > MaxChunks)
	{
		return M5ChunkSetError(E::InvalidInput, TEXT("Invalid chunk count or limit"));
	}
	int64 Count = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Size = int64(MaxExclusive[Axis]) - int64(MinInclusive[Axis]);
		if (Size < 1 || Size > MaxChunks || Count > MaxChunks / Size)
		{
			return M5ChunkSetError(E::InvalidInput, TEXT("Invalid or over-budget chunk rectangle"));
		}
		Count *= Size;
	}
	if (Count != Chunks.Num())
	{
		return M5ChunkSetError(E::Unproven, TEXT("Chunk rectangle is incomplete"));
	}
	// At most N(N-1)/2 duplicate comparisons, 3*N*N neighbor searches,
	// 3*N*3*35*35 shared sample comparisons, row cancellation checks,
	// and N input checks. N <= 512; the extra quadratic and linear slack
	// also cover bounded cancellation checks in duplicate/neighbor searches.
	const int64 Work = 4 * Count * Count + Count * 12000 + 1;
	if (!Budget->TryChargeWork(Work))
	{
		return M5ChunkSetError(E::BudgetExceeded, TEXT("Chunk-set work budget exhausted"));
	}
	for (int32 I = 0; I < Chunks.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
		}
		const auto& Chunk = Chunks[I];
		if (Chunk.Key.LOD != 0 || Chunk.Density.Num() != FVoxelLandChunkData::SampleCount)
		{
			return M5ChunkSetError(E::InvalidInput, TEXT("Malformed LOD-zero chunk"));
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Chunk.Key.Coordinate[Axis] < MinInclusive[Axis] || Chunk.Key.Coordinate[Axis] >= MaxExclusive[Axis])
			{
				return M5ChunkSetError(E::OutOfDomain, TEXT("Chunk is outside the requested rectangle"));
			}
		}
		for (int32 J = 0; J < I; ++J)
		{
			if ((J & 63) == 0 && IsCancelled())
			{
				return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
			}
			if (Chunks[J].Key.Coordinate == Chunk.Key.Coordinate)
			{
				return M5ChunkSetError(E::InvalidInput, TEXT("Duplicate chunk coordinate"));
			}
		}
	}
	for (const auto& Chunk : Chunks)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (int64(Chunk.Key.Coordinate[Axis]) + 1 >= int64(MaxExclusive[Axis]))
			{
				continue;
			}
			FIntVector NeighborCoordinate = Chunk.Key.Coordinate;
			++NeighborCoordinate[Axis]; // Strictly below int32 MaxExclusive.
			const FVoxelLandChunkData* Neighbor = nullptr;
			for (int32 J = 0; J < Chunks.Num(); ++J)
			{
				if ((J & 63) == 0 && IsCancelled())
				{
					return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
				}
				if (Chunks[J].Key.Coordinate == NeighborCoordinate)
				{
					Neighbor = &Chunks[J];
					break;
				}
			}
			if (!Neighbor)
			{
				return M5ChunkSetError(E::Unproven, TEXT("Neighbor chunk missing"));
			}
			for (int32 Plane = 0; Plane < 3; ++Plane)
			{
				for (int32 A = 0; A < 35; ++A)
				{
					if (IsCancelled())
					{
						return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
					}
					for (int32 B = 0; B < 35; ++B)
					{
						int32 Left[3] = {A, B, B}, Right[3] = {A, B, B};
						Left[Axis] = 32 + Plane;
						Right[Axis] = Plane;
						Left[(Axis + 1) % 3] = Right[(Axis + 1) % 3] = A;
						Left[(Axis + 2) % 3] = Right[(Axis + 2) % 3] = B;
						if (Chunk.Density[M5ChunkSetIndex(Left[0], Left[1], Left[2])]
							!= Neighbor->Density[M5ChunkSetIndex(Right[0], Right[1], Right[2])])
						{
							return M5ChunkSetError(E::Unproven, TEXT("Adjacent chunk sample halos disagree"));
						}
					}
				}
			}
		}
	}
	if (IsCancelled())
	{
		return M5ChunkSetError(E::Cancelled, TEXT("Chunk-set check cancelled"));
	}
	return {};
}
