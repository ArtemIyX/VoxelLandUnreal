#pragma once

#include "CoreMinimal.h"

// Version 1 regional stream and feature-key encodings. A digest contains all
// 32 bytes; callers using shortened IDs must retain full keys and reject
// collisions. All output parameters are unchanged on failure.
namespace VoxelLand::M5::RegionalSeed
{
VOXELLANDALGO_API bool TryStreamPacket(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	int64 RegionX, int64 RegionY, uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutPacket);
VOXELLANDALGO_API bool TryStreamDigest(uint8 Namespace, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	int64 RegionX, int64 RegionY, uint64 ElementId, uint32 Attempt, uint32 Draw, TArray<uint8>& OutDigest);

// Kinds 1 and 3 are candidate and entrance: KeyB must be zero. Kind 2 is an
// undirected edge; endpoint keys are canonicalized and must be distinct.
VOXELLANDALGO_API bool TryFeaturePacket(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX,
	int64 RegionY, uint8 FeatureKind, uint64 KeyA, uint64 KeyB, TArray<uint8>& OutPacket);
VOXELLANDALGO_API bool TryFeatureDigest(int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 RegionX,
	int64 RegionY, uint8 FeatureKind, uint64 KeyA, uint64 KeyB, TArray<uint8>& OutDigest);
}
