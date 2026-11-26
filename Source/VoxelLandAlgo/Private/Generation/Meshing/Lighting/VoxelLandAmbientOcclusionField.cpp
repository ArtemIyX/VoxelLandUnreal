#include "VoxelLandAmbientOcclusionField.h"

#include "../../Clipmap/Sampling/VoxelLandCompletedBox.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Meshing/Lighting/VoxelLandTerrainAmbientOcclusion.h"

FVoxelLandAmbientOcclusionField::FVoxelLandAmbientOcclusionField(const FVoxelLandGenerator& InGenerator,
	FVoxelLandWorkerContext& InWorker, TFunctionRef<bool()> InIsCancelled, const FVoxelLandChunkData* InLOD0Data)
	: Generator(InGenerator)
	, Worker(InWorker)
	, IsCancelled(InIsCancelled)
	, LOD0Data(InLOD0Data)
	, bHasHeight(InGenerator.SupportsM7CompletedField())
	, LocalHeightSource(InGenerator.M4Unlimited.Get())
{
}

bool FVoxelLandAmbientOcclusionField::Supports(const FVoxelLandGenerator& Generator, uint8 Lod)
{
	return Generator.SupportsM7CompletedField()
		|| (Lod == 0 && !Generator.M5Lease && !Generator.M4HeightGrid && !Generator.M4Unlimited);
}

bool FVoxelLandAmbientOcclusionField::TryReadChunk(
	const FVoxelLandChunkData& Chunk, const FInt64Vector& Position, int16& OutDensity)
{
	constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
	const FInt64Vector First = FInt64Vector(Chunk.Key.Coordinate) * int64(FVoxelLandChunkData::Cells) - FInt64Vector(1);
	const FInt64Vector Local = Position - First;
	if (Local.X < 0 || Local.X >= Axis || Local.Y < 0 || Local.Y >= Axis || Local.Z < 0 || Local.Z >= Axis)
	{
		return false;
	}
	OutDensity = Chunk.Density[static_cast<int32>(Local.X + Axis * (Local.Y + Axis * Local.Z))];
	return true;
}

FVoxelLandError FVoxelLandAmbientOcclusionField::operator()(const FInt64Vector& Position, int16& OutDensity)
{
	if (!bHasHeight)
	{
		return ReadDirect(Position, OutDensity);
	}
	if (LOD0Data && TryReadChunk(*LOD0Data, Position, OutDensity))
	{
		return {};
	}
	return ReadHeight(Position, OutDensity);
}

FVoxelLandError FVoxelLandAmbientOcclusionField::ReadHeightBox(const FVoxelLandM7CompletedBox& Box, TArray<int16>& Out)
{
	constexpr int64 MaxHeightWork = 16 * 1024 * 1024;
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Terrain ambient visibility field reads cancelled")};
	}
	if (Box.Work > MaxHeightWork - PlannedHeightWork)
	{
		return {EVoxelLandErrorCode::InvalidParameter,
			TEXT("Terrain ambient visibility exceeds the completed field work budget")};
	}
	PlannedHeightWork += Box.Work;
	TFunctionRef<bool(int64, int64, int32&)> LocalHeight(LocalHeightSource);
	TFunctionRef<bool(int64, int64, int32&)>* Height = Generator.IsM4Unlimited() ? &LocalHeight : nullptr;
	return LocalHeightSource.PreferSourceError(Generator.ReadM7CompletedBox(Box, Worker, Out, IsCancelled, Height));
}

FVoxelLandError FVoxelLandAmbientOcclusionField::ReadHeightEdge(const FInt64Vector& Position, int16& OutDensity)
{
	if (const int16* Cached = HeightEdgeSamples.Find(Position))
	{
		OutDensity = *Cached;
		return {};
	}
	constexpr int32 MaxHeightEdgeSamples = 32 * 1024;
	if (HeightEdgeSamples.Num() >= MaxHeightEdgeSamples)
	{
		return {EVoxelLandErrorCode::InvalidParameter,
			TEXT("Terrain ambient visibility exceeds the finite edge sample budget")};
	}

	FVoxelLandM7CompletedBox Box;
	if (const FVoxelLandError Error = Generator.PlanM7CompletedBox(Position, 1, Box))
	{
		if (Error.Code != EVoxelLandErrorCode::InvalidParameter
			&& Error.Code != EVoxelLandErrorCode::NonFiniteCoordinate)
		{
			return Error;
		}
		// A sample outside the admitted height footprint supplies no occluder.
		OutDensity = MAX_int16;
	}
	else
	{
		TArray<int16> Values;
		if (const FVoxelLandError ReadError = ReadHeightBox(Box, Values))
		{
			return ReadError;
		}
		OutDensity = Values[0];
	}
	HeightEdgeSamples.Add(Position, OutDensity);
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionField::ReadHeight(const FInt64Vector& Position, int16& OutDensity)
{
	constexpr int32 TileAxis = 8;
	constexpr int32 MaxHeightTiles = 8 * 1024;
	const FInt64Vector First(VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.X), TileAxis) * TileAxis,
		VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.Y), TileAxis) * TileAxis,
		VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.Z), TileAxis) * TileAxis);
	TArray<int16>* Values = HeightTiles.Find(First);
	if (!Values)
	{
		if (HeightTiles.Num() >= MaxHeightTiles)
		{
			return {EVoxelLandErrorCode::InvalidParameter,
				TEXT("Terrain ambient visibility exceeds the height tile budget")};
		}
		FVoxelLandM7CompletedBox Box;
		Values = &HeightTiles.Add(First);
		if (const FVoxelLandError Error = Generator.PlanM7CompletedBox(First, TileAxis, Box))
		{
			if (Error.Code != EVoxelLandErrorCode::InvalidParameter
				&& Error.Code != EVoxelLandErrorCode::NonFiniteCoordinate)
			{
				return Error;
			}
		}
		else if (const FVoxelLandError ReadError = ReadHeightBox(Box, *Values))
		{
			return ReadError;
		}
	}
	if (Values->IsEmpty())
	{
		// A tile may straddle the finite footprint even when this individual sample is valid.
		return ReadHeightEdge(Position, OutDensity);
	}
	const FInt64Vector Local = Position - First;
	OutDensity = (*Values)[static_cast<int32>(Local.X + TileAxis * (Local.Y + TileAxis * Local.Z))];
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionField::ReadDirect(const FInt64Vector& Position, int16& OutDensity)
{
	constexpr int32 Cells = FVoxelLandChunkData::Cells;
	FVoxelLandChunkKey Key;
	Key.Coordinate = FIntVector(VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.X), Cells),
		VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.Y), Cells),
		VoxelLandCoordinates::FloorDivide(static_cast<int32>(Position.Z), Cells));

	if (const TOptional<FVoxelLandDensityBounds>& Bounds = Generator.GetDirectDensityBounds(); Bounds.IsSet())
	{
		const FInt64Vector Min = FInt64Vector(Bounds->MinChunk) * int64(Cells);
		const FInt64Vector Max = FInt64Vector(Bounds->MaxChunkExclusive) * int64(Cells);
		const FInt64Vector BeforeMin = Min - Position;
		const FInt64Vector AfterMax = Position - Max;
		const int64 Distance = FMath::Max(BeforeMin.GetMax(), AfterMax.GetMax());
		if (Distance > 1)
		{
			// The box exterior is air. Its immediate halo still comes from the clipped, completed chunk.
			OutDensity = MAX_int16;
			return {};
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Key.Coordinate[Axis] =
				FMath::Clamp(Key.Coordinate[Axis], Bounds->MinChunk[Axis], Bounds->MaxChunkExclusive[Axis] - 1);
		}
	}

	// The chunk's one-cell overlap is part of the same completed lattice.
	if (LOD0Data && TryReadChunk(*LOD0Data, Position, OutDensity))
	{
		return {};
	}
	FChunkPtr* Chunk = DirectChunks.Find(Key.Coordinate);
	if (!Chunk)
	{
		constexpr int32 MaxDirectChunks = 27;
		if (DirectChunks.Num() >= MaxDirectChunks)
		{
			return {EVoxelLandErrorCode::InvalidParameter,
				TEXT("Terrain ambient visibility exceeds the direct density halo budget")};
		}
		FChunkPtr Generated;
		if (const FVoxelLandError Error = Generator.GenerateChunkCancellable(Key, Worker, Generated, IsCancelled))
		{
			return Error;
		}
		Chunk = &DirectChunks.Add(Key.Coordinate, MoveTemp(Generated));
	}
	if (!Chunk->IsValid() || (*Chunk)->Density.Num() != FVoxelLandChunkData::SampleCount
		|| !TryReadChunk(**Chunk, Position, OutDensity))
	{
		return {EVoxelLandErrorCode::InvalidContext,
			TEXT("Terrain ambient visibility received an incomplete direct density halo")};
	}
	return {};
}

FVoxelLandError FVoxelLandTerrainAmbientOcclusion::Bake(FVoxelLandMeshData& Mesh, const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker, const FVoxelLandChunkKey& ChunkKey, const FVector3d& LogicalOrigin,
	TFunctionRef<bool()> IsCancelled, const FVoxelLandChunkData* LOD0Data)
{
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Terrain ambient visibility cancelled")};
	}
	if (!FVoxelLandAmbientOcclusionField::Supports(Generator, ChunkKey.LOD))
	{
		Mesh.AmbientVisibility.Reset();
		return {};
	}
	if (IsInGameThread() || !Generator.OwnsWorkerOnCurrentThread(Worker)
		|| (LOD0Data
			&& (ChunkKey.LOD != 0 || !(LOD0Data->Key == ChunkKey)
				|| LOD0Data->Density.Num() != FVoxelLandChunkData::SampleCount)))
	{
		return {EVoxelLandErrorCode::InvalidContext,
			TEXT("Terrain ambient visibility requires its owning field worker and a matching chunk")};
	}
	FVoxelLandAmbientOcclusionField Source(Generator, Worker, IsCancelled, LOD0Data);
	return Bake(Mesh, Generator.GetProfile().VoxelSizeCm, LogicalOrigin, Source, IsCancelled);
}
