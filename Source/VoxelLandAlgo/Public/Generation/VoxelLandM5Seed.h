#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

// M5 seed contract version 1. Integers are encoded little-endian; GUIDs are
// their A, B, C, D uint32 words in that order. No UObject or global RNG state.
// Every output is unchanged on failure. A digest contains all 32 SHA-256 bytes.
namespace VoxelLand::M5::Seed
{
// Standard SHA-256 for bounded packets of at most 119 bytes. Output is unchanged on failure.
VOXELLANDALGO_API bool TrySha256(TConstArrayView<uint8> Bytes, TArray<uint8>& OutDigest);

VOXELLANDALGO_API bool TryStreamPacket(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutPacket);
VOXELLANDALGO_API bool TryStreamDigest(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutDigest);

// The two signed XY regions must be face-adjacent. Input order is immaterial.
VOXELLANDALGO_API bool TryPortalPacket(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 AX, int64 AY, int64 BX,
	int64 BY, TArray<uint8>& OutPacket);
VOXELLANDALGO_API bool TryPortalDigest(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 AX, int64 AY, int64 BX,
	int64 BY, TArray<uint8>& OutDigest);

// Digest's first eight bytes interpreted as a little-endian word.
VOXELLANDALGO_API bool TryDigestWord(TConstArrayView<uint8> Digest, uint64& OutWord);

// Uniform result in [0, Range); rejects Range == 0. Source gets an explicit
// draw index; source failure, cancellation, exhausted budget or draw-index
// overflow returns false without modifying OutValue or OutDrawsUsed.
VOXELLANDALGO_API bool TryUniform(uint64 Range, uint32 FirstDraw, uint32 MaxDraws,
	TFunctionRef<bool(uint32, uint64&)> Source, TFunctionRef<bool()> IsCancelled, uint64& OutValue,
	uint32& OutDrawsUsed);

// Inclusive signed interval, including the entire int64 range.
VOXELLANDALGO_API bool TryUniformSigned(int64 Min, int64 Max, uint32 FirstDraw, uint32 MaxDraws,
	TFunctionRef<bool(uint32, uint64&)> Source, TFunctionRef<bool()> IsCancelled, int64& OutValue,
	uint32& OutDrawsUsed);
}
