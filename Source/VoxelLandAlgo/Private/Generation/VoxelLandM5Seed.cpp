#include "Generation/VoxelLandM5Seed.h"

class FVoxelLandM5SeedInternals final
{
public:
	static constexpr int32 Sha256BlockBytes = 64;
	static constexpr int32 Sha256LengthBytes = sizeof(uint64);
	static constexpr int32 Sha256MaxBlocks = 2;
	static constexpr int32 Sha256MaxInputBytes = Sha256MaxBlocks * Sha256BlockBytes - Sha256LengthBytes - 1;
	static constexpr int32 Sha256Rounds = 64;
	static constexpr int32 Sha256DigestBytes = 32;
	static constexpr int32 Sha256FirstBlockPayloadBytes = Sha256BlockBytes - Sha256LengthBytes;

	static void M5Append32(TArray<uint8>& Bytes, uint32 Value);
	static void M5Append64(TArray<uint8>& Bytes, uint64 Value);
	static void M5AppendGuid(TArray<uint8>& Bytes, const FGuid& Guid);
	static uint32 M5RotateRight(uint32 X, uint32 Count);
	static void M5Sha256(TConstArrayView<uint8> Bytes, TArray<uint8>& Out);
};

void FVoxelLandM5SeedInternals::M5Append32(TArray<uint8>& Bytes, uint32 Value)
{
	for (uint32 Shift = 0; Shift < 32; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

void FVoxelLandM5SeedInternals::M5Append64(TArray<uint8>& Bytes, uint64 Value)
{
	for (uint32 Shift = 0; Shift < 64; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

void FVoxelLandM5SeedInternals::M5AppendGuid(TArray<uint8>& Bytes, const FGuid& Guid)
{
	M5Append32(Bytes, Guid.A);
	M5Append32(Bytes, Guid.B);
	M5Append32(Bytes, Guid.C);
	M5Append32(Bytes, Guid.D);
}

uint32 FVoxelLandM5SeedInternals::M5RotateRight(uint32 X, uint32 Count)
{
	return (X >> Count) | (X << (32 - Count));
}

// SHA-256 compression words and additions use uint32 for defined wraparound.
void FVoxelLandM5SeedInternals::M5Sha256(TConstArrayView<uint8> Bytes, TArray<uint8>& Out)
{
	static constexpr uint32 K[Sha256Rounds] = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
		0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u,
		0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
		0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u,
		0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u,
		0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
		0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u,
		0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau,
		0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
	uint32 State[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
	uint8 Padded[Sha256BlockBytes * Sha256MaxBlocks] = {};
	const int32 Length = Bytes.Num();
	if (Length != 0)
	{
		FMemory::Memcpy(Padded, Bytes.GetData(), Length);
	}
	Padded[Length] = 0x80;
	const uint64 BitLength = static_cast<uint64>(Length) * 8;
	const int32 BlockCount = Length < Sha256FirstBlockPayloadBytes ? 1 : Sha256MaxBlocks;
	for (int32 I = 0; I < Sha256LengthBytes; ++I)
	{
		Padded[BlockCount * Sha256BlockBytes - 1 - I] = static_cast<uint8>(BitLength >> (I * 8));
	}
	for (int32 Block = 0; Block < BlockCount; ++Block)
	{
		uint32 W[Sha256Rounds];
		for (int32 I = 0; I < 16; ++I)
		{
			const int32 Offset = Block * Sha256BlockBytes + I * static_cast<int32>(sizeof(uint32));
			W[I] = (uint32(Padded[Offset]) << 24) | (uint32(Padded[Offset + 1]) << 16)
				 | (uint32(Padded[Offset + 2]) << 8) | uint32(Padded[Offset + 3]);
		}
		for (int32 I = 16; I < Sha256Rounds; ++I)
		{
			const uint32 A = M5RotateRight(W[I - 15], 7) ^ M5RotateRight(W[I - 15], 18) ^ (W[I - 15] >> 3);
			const uint32 B = M5RotateRight(W[I - 2], 17) ^ M5RotateRight(W[I - 2], 19) ^ (W[I - 2] >> 10);
			W[I] = W[I - 16] + A + W[I - 7] + B;
		}
		uint32 A = State[0], B = State[1], C = State[2], D = State[3];
		uint32 E = State[4], F = State[5], G = State[6], H = State[7];
		for (int32 I = 0; I < Sha256Rounds; ++I)
		{
			const uint32 Sum1 = M5RotateRight(E, 6) ^ M5RotateRight(E, 11) ^ M5RotateRight(E, 25);
			const uint32 Choice = (E & F) ^ (~E & G);
			const uint32 T1 = H + Sum1 + Choice + K[I] + W[I];
			const uint32 Sum0 = M5RotateRight(A, 2) ^ M5RotateRight(A, 13) ^ M5RotateRight(A, 22);
			const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
			const uint32 T2 = Sum0 + Majority;
			H = G;
			G = F;
			F = E;
			E = D + T1;
			D = C;
			C = B;
			B = A;
			A = T1 + T2;
		}
		State[0] += A;
		State[1] += B;
		State[2] += C;
		State[3] += D;
		State[4] += E;
		State[5] += F;
		State[6] += G;
		State[7] += H;
	}
	TArray<uint8> Digest;
	Digest.Reserve(Sha256DigestBytes);
	for (uint32 Word : State)
	{
		for (int32 Shift = 24; Shift >= 0; Shift -= 8)
		{
			Digest.Add(static_cast<uint8>(Word >> Shift));
		}
	}
	Out = MoveTemp(Digest);
}

namespace VoxelLand::M5::Seed
{
bool TrySha256(TConstArrayView<uint8> Bytes, TArray<uint8>& OutDigest)
{
	if (Bytes.Num() > FVoxelLandM5SeedInternals::Sha256MaxInputBytes)
	{
		return false;
	}
	FVoxelLandM5SeedInternals::M5Sha256(Bytes, OutDigest);
	return true;
}

bool TryStreamPacket(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt, uint64 ElementId,
	uint32 Attempt, uint32 Draw, TArray<uint8>& OutPacket)
{
	if (Namespace < 1 || Namespace > 5)
	{
		return false;
	}
	TArray<uint8> Bytes = {'V', 'L', 'M', '5', 'R', 'N', 'G', 0};
	Bytes.Reserve(61);
	FVoxelLandM5SeedInternals::M5Append32(Bytes, 1);
	Bytes.Add(Namespace);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(WorldSeed));
	FVoxelLandM5SeedInternals::M5AppendGuid(Bytes, StageId);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, Salt);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, ElementId);
	FVoxelLandM5SeedInternals::M5Append32(Bytes, Attempt);
	FVoxelLandM5SeedInternals::M5Append32(Bytes, Draw);
	OutPacket = MoveTemp(Bytes);
	return true;
}

bool TryStreamDigest(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt, uint64 ElementId,
	uint32 Attempt, uint32 Draw, TArray<uint8>& OutDigest)
{
	TArray<uint8> Bytes;
	if (!TryStreamPacket(Namespace, WorldSeed, StageId, Salt, ElementId, Attempt, Draw, Bytes))
	{
		return false;
	}
	FVoxelLandM5SeedInternals::M5Sha256(Bytes, OutDigest);
	return true;
}

bool TryPortalPacket(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 AX, int64 AY, int64 BX, int64 BY,
	TArray<uint8>& OutPacket)
{
	// Avoid subtracting arbitrary signed coordinates when checking adjacency.
	if (AX > BX || (AX == BX && AY > BY))
	{
		Swap(AX, BX);
		Swap(AY, BY);
	}
	const bool bXFace = AY == BY && AX != MAX_int64 && BX == AX + 1;
	const bool bYFace = AX == BX && AY != MAX_int64 && BY == AY + 1;
	if (!bXFace && !bYFace)
	{
		return false;
	}
	TArray<uint8> Bytes = {'V', 'L', 'M', '5', 'P', 'R', 'T', 0};
	Bytes.Reserve(77);
	FVoxelLandM5SeedInternals::M5Append32(Bytes, 1);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(WorldSeed));
	FVoxelLandM5SeedInternals::M5AppendGuid(Bytes, StageId);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, Salt);
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(AX));
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(AY));
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(BX));
	FVoxelLandM5SeedInternals::M5Append64(Bytes, static_cast<uint64>(BY));
	Bytes.Add(bXFace ? 0 : 1);
	OutPacket = MoveTemp(Bytes);
	return true;
}

bool TryPortalDigest(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 AX, int64 AY, int64 BX, int64 BY,
	TArray<uint8>& OutDigest)
{
	TArray<uint8> Bytes;
	if (!TryPortalPacket(WorldSeed, StageId, Salt, AX, AY, BX, BY, Bytes))
	{
		return false;
	}
	FVoxelLandM5SeedInternals::M5Sha256(Bytes, OutDigest);
	return true;
}

bool TryDigestWord(TConstArrayView<uint8> Digest, uint64& OutWord)
{
	if (Digest.Num() != 32)
	{
		return false;
	}
	uint64 Word = 0;
	for (uint32 I = 0; I < 8; ++I)
	{
		Word |= uint64(Digest[I]) << (I * 8);
	}
	OutWord = Word;
	return true;
}

bool TryUniform(uint64 Range, uint32 FirstDraw, uint32 DrawLimit, TFunctionRef<bool(uint32, uint64&)> Source,
	TFunctionRef<bool()> IsCancelled, uint64& OutValue, uint32& OutDrawsUsed)
{
	if (Range == 0 || DrawLimit == 0)
	{
		return false;
	}
	const uint64 Threshold = (uint64(0) - Range) % Range;
	for (uint32 Index = 0; Index < DrawLimit; ++Index)
	{
		if (IsCancelled() || Index > MAX_uint32 - FirstDraw)
		{
			return false;
		}
		uint64 Draw = 0;
		if (!Source(FirstDraw + Index, Draw))
		{
			return false;
		}
		if (Draw >= Threshold)
		{
			// Check again before publishing; the producer may have observed cancellation.
			if (IsCancelled())
			{
				return false;
			}
			OutValue = Draw % Range;
			OutDrawsUsed = Index + 1;
			return true;
		}
	}
	return false;
}

bool TryUniformSigned(int64 Min, int64 Max, uint32 FirstDraw, uint32 DrawLimit,
	TFunctionRef<bool(uint32, uint64&)> Source, TFunctionRef<bool()> IsCancelled, int64& OutValue, uint32& OutDrawsUsed)
{
	if (Min > Max || DrawLimit == 0)
	{
		return false;
	}
	constexpr uint64 SignBit = uint64(1) << 63;
	const uint64 LowKey = static_cast<uint64>(Min) ^ SignBit;
	const uint64 HighKey = static_cast<uint64>(Max) ^ SignBit;
	const uint64 Range = HighKey - LowKey + uint64(1); // Zero denotes the full 2^64 range.
	uint64 Offset = 0;
	uint32 DrawsUsed = 0;
	if (Range == 0)
	{
		if (IsCancelled() || !Source(FirstDraw, Offset) || IsCancelled())
		{
			return false;
		}
		DrawsUsed = 1;
	}
	else if (!TryUniform(Range, FirstDraw, DrawLimit, Source, IsCancelled, Offset, DrawsUsed))
	{
		return false;
	}
	const uint64 Key = LowKey + Offset; // Offset never exceeds HighKey - LowKey.
	const int64 Result = Key < SignBit ? MIN_int64 + static_cast<int64>(Key) : static_cast<int64>(Key - SignBit);
	OutValue = Result;
	OutDrawsUsed = DrawsUsed;
	return true;
}
}
