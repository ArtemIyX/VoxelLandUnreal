#include "Generation/VoxelLandM5PocketNoise.h"

#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5Seed.h"

class FVoxelLandM5PocketNoiseInternals final
{
public:
	static constexpr int64 M5PocketLimitCm = 1048576;
	static constexpr int64 M5PocketOne = 65536;
	static void Append32(TArray<uint8>& Bytes, uint32 Value);
	static void Append64(TArray<uint8>& Bytes, uint64 Value);
	static int32 Interpolate(int32 A, int32 B, int64 Weight);
};

void FVoxelLandM5PocketNoiseInternals::Append32(TArray<uint8>& Bytes, uint32 Value)
{
	for (uint32 Shift = 0; Shift < 32; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

void FVoxelLandM5PocketNoiseInternals::Append64(TArray<uint8>& Bytes, uint64 Value)
{
	for (uint32 Shift = 0; Shift < 64; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

// Bounds guarantee a numerator in [-2^31, 2^31); floor is explicit for
// negative results because C++ signed division truncates toward zero.
int32 FVoxelLandM5PocketNoiseInternals::Interpolate(int32 A, int32 B, int64 Weight)
{
	const int64 Numerator = int64(A) * (M5PocketOne - Weight) + int64(B) * Weight;
	int64 Value = Numerator / M5PocketOne;
	if (Numerator % M5PocketOne < 0)
	{
		--Value;
	}
	return static_cast<int32>(Value);
}

namespace VoxelLand::M5::PocketNoise
{
bool TryCornerPacket(
	int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 CellX, int64 CellY, int64 CellZ, TArray<uint8>& OutPacket)
{
	TArray<uint8> Packet = {'V', 'L', 'M', '5', 'P', 'C', 'K', 0};
	Packet.Reserve(68);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, 1);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(WorldSeed));
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.A);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.B);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.C);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.D);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, Salt);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellX));
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellY));
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellZ));
	OutPacket = MoveTemp(Packet);
	return true;
}

bool TrySample(int64 X, int64 Y, int64 Z, int64 PeriodCm, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	TFunctionRef<bool()> IsCancelled, int32& OutValue)
{
	if (PeriodCm <= 0 || PeriodCm > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| X < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| X > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Y < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Y > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Z < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Z > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm)
	{
		return false;
	}
	const int64 Position[3] = {X, Y, Z};
	int64 Cell[3];
	int64 Weight[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!CheckedMath::TryFloorDivide(Position[Axis], PeriodCm, Cell[Axis]))
		{
			return false;
		}
		// Validated inputs bound this multiplication and subtraction.
		const int64 Remainder = Position[Axis] - Cell[Axis] * PeriodCm;
		Weight[Axis] = (Remainder * FVoxelLandM5PocketNoiseInternals::M5PocketOne) / PeriodCm;
	}
	int32 Corner[2][2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			for (int32 I = 0; I < 2; ++I)
			{
				if (IsCancelled())
				{
					return false;
				}
				TArray<uint8> Packet;
				TArray<uint8> Digest;
				if (!TryCornerPacket(WorldSeed, StageId, Salt, Cell[0] + I, Cell[1] + J, Cell[2] + K, Packet)
					|| !Seed::TrySha256(Packet, Digest) || Digest.Num() != 32)
				{
					return false;
				}
				Corner[K][J][I] = int32(uint32(Digest[0]) | (uint32(Digest[1]) << 8)) - 32768;
			}
		}
	}
	int32 AlongX[2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			AlongX[K][J] = FVoxelLandM5PocketNoiseInternals::Interpolate(Corner[K][J][0], Corner[K][J][1], Weight[0]);
		}
	}
	const int32 Y0 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[0][0], AlongX[0][1], Weight[1]);
	const int32 Y1 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[1][0], AlongX[1][1], Weight[1]);
	const int32 Value = FVoxelLandM5PocketNoiseInternals::Interpolate(Y0, Y1, Weight[2]);
	if (IsCancelled())
	{
		return false;
	}
	OutValue = Value;
	return true;
}
}
