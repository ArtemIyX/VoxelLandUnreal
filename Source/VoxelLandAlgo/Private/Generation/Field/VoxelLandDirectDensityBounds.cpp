#include "Generation/Field/VoxelLandGenerator.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Hash/xxhash.h"

bool FVoxelLandGenerator::IncludesChunk(const FVoxelLandChunkKey& Key) const
{
	return !DirectDensityBounds.IsSet() || (Key.LOD == 0 && DirectDensityBounds->Contains(Key.Coordinate));
}

FVoxelLandError FVoxelLandGenerator::ClipDirectDensityChunk(
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Chunk, TFunctionRef<bool()> IsCancelled) const
{
	const FVoxelLandDensityBounds& Bounds = DirectDensityBounds.GetValue();
	constexpr int32 Cells = FVoxelLandChunkData::Cells;
	constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int64 DensityUnitsPerVoxel = 1024;
	const FInt64Vector Base = FInt64Vector(Chunk->Key.Coordinate) * int64(Cells);
	const FInt64Vector Min = FInt64Vector(Bounds.MinChunk) * int64(Cells);
	const FInt64Vector Max = FInt64Vector(Bounds.MaxChunkExclusive) * int64(Cells);
	int16 PlaneDistances[3][Axis];
	bool bTouchesBoundary = false;
	for (int32 Dimension = 0; Dimension < 3; ++Dimension)
	{
		for (int32 Local = 0; Local < Axis; ++Local)
		{
			const int64 Position = Base[Dimension] + Local - 1;
			const int64 Distance = FMath::Max(Min[Dimension] - Position, Position - Max[Dimension]);
			const int64 Quantized = FMath::Clamp(Distance * DensityUnitsPerVoxel, int64(-32767), int64(32767));
			PlaneDistances[Dimension][Local] = int16(Quantized);
			bTouchesBoundary |= Quantized > -32767;
		}
	}
	if (!bTouchesBoundary)
	{
		return {};
	}
	TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Clipped =
		MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>(*Chunk);
	int32 Index = 0;
	for (int32 Z = 0; Z < Axis; ++Z)
	{
		if (IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Finite direct density clipping cancelled")};
		}
		for (int32 Y = 0; Y < Axis; ++Y)
		{
			const int16 DistanceYZ = FMath::Max(PlaneDistances[1][Y], PlaneDistances[2][Z]);
			for (int32 X = 0; X < Axis; ++X, ++Index)
			{
				const int16 Boundary = FMath::Max(PlaneDistances[0][X], DistanceYZ);
				Clipped->Density[Index] = FMath::Max(Clipped->Density[Index], Boundary);
			}
		}
	}
	Clipped->Hash = VoxelLandDeterminism::HashDensity(Clipped->Density);
	Chunk = Clipped;
	return {};
}

uint64 FVoxelLandGenerator::GetDirectDensityIdentity() const
{
	FCanonicalWriter Writer;
	Writer.U32(1);
	const uint64 Fingerprint = Program->GetFingerprint();
	Writer.U32(uint32(Fingerprint));
	Writer.U32(uint32(Fingerprint >> 32));
	Writer.U32(uint32(DirectDensitySeed));
	Writer.U32(uint32(DirectDensitySeed >> 32));
	Writer.Float(Profile.VoxelSizeCm);
	Writer.U32(uint32(FloatParameters.Num()));
	for (const float Value : FloatParameters)
	{
		Writer.Float(Value);
	}
	Writer.U32(uint32(IntParameters.Num()));
	for (const int32 Value : IntParameters)
	{
		Writer.U32(uint32(Value));
	}
	Writer.U8(DirectDensityBounds.IsSet() ? 1 : 0);
	if (DirectDensityBounds.IsSet())
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Writer.U32(uint32(DirectDensityBounds->MinChunk[Axis]));
			Writer.U32(uint32(DirectDensityBounds->MaxChunkExclusive[Axis]));
		}
	}
	return FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
}
