#include "Generation/Caves/Planning/VoxelLandCaveRegionalSeed.h"

#include "Generation/Caves/Planning/VoxelLandCaveSeed.h"

class FVoxelLandM5RegionalSeedInternals final
{
public:
	static void M5RegionalAppend32(TArray<uint8>& Bytes, uint32 Value)
	{
		for (uint32 Shift = 0; Shift < 32; Shift += 8)
		{
			Bytes.Add(static_cast<uint8>(Value >> Shift));
		}
	}

	static void M5RegionalAppend64(TArray<uint8>& Bytes, uint64 Value)
	{
		for (uint32 Shift = 0; Shift < 64; Shift += 8)
		{
			Bytes.Add(static_cast<uint8>(Value >> Shift));
		}
	}
};

namespace VoxelLand::M5::RegionalSeed
{
bool TryStreamPacket(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX, int64 RegionY,
	uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutPacket)
{
	TArray<uint8> Generic;
	if (!Seed::TryStreamPacket(Namespace, WorldSeed, StageId, Salt, ElementId, Attempt, Draw, Generic))
	{
		return false;
	}
	TArray<uint8> Bytes = {'V', 'L', 'M', '5', 'R', 'E', 'G', 0};
	Bytes.Reserve(89);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, 1);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, static_cast<uint64>(RegionX));
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, static_cast<uint64>(RegionY));
	Bytes.Append(Generic);
	OutPacket = MoveTemp(Bytes);
	return true;
}

bool TryStreamDigest(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX, int64 RegionY,
	uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutDigest)
{
	TArray<uint8> Bytes;
	return TryStreamPacket(Namespace, WorldSeed, StageId, Salt, RegionX, RegionY, ElementId, Attempt, Draw, Bytes)
		&& Seed::TrySha256(Bytes, OutDigest);
}

bool TryFeaturePacket(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX, int64 RegionY,
	uint8 FeatureKind, uint64 KeyA, uint64 KeyB, TArray<uint8>& OutPacket)
{
	if (FeatureKind < 1 || FeatureKind > 3 || (FeatureKind != 2 && KeyB != 0) || (FeatureKind == 2 && KeyA == KeyB))
	{
		return false;
	}
	if (FeatureKind == 2 && KeyA > KeyB)
	{
		Swap(KeyA, KeyB);
	}
	TArray<uint8> Bytes = {'V', 'L', 'M', '5', 'F', 'I', 'D', 0};
	Bytes.Reserve(77);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, 1);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, static_cast<uint64>(WorldSeed));
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, StageId.A);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, StageId.B);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, StageId.C);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend32(Bytes, StageId.D);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, Salt);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, static_cast<uint64>(RegionX));
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, static_cast<uint64>(RegionY));
	Bytes.Add(FeatureKind);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, KeyA);
	FVoxelLandM5RegionalSeedInternals::M5RegionalAppend64(Bytes, KeyB);
	OutPacket = MoveTemp(Bytes);
	return true;
}

bool TryFeatureDigest(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX, int64 RegionY,
	uint8 FeatureKind, uint64 KeyA, uint64 KeyB, TArray<uint8>& OutDigest)
{
	TArray<uint8> Bytes;
	return TryFeaturePacket(WorldSeed, StageId, Salt, RegionX, RegionY, FeatureKind, KeyA, KeyB, Bytes)
		&& Seed::TrySha256(Bytes, OutDigest);
}
}
