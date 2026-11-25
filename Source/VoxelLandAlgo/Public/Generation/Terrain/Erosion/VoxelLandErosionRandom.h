#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"

/** Experimental deterministic droplet-start stream. Not a persisted world format. */
namespace VoxelLandM4ErosionRandom
{
// Version 1: all arithmetic is unsigned modulo 2^64. Derive the stage seed using
// VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, StageId, Salt); its GUID input
// is the numeric A,B,C,D uint32 words, paired as (A<<32)|B and (C<<32)|D.
// No native object bytes, host endianness or FName indices participate.
// DropletSeed = Mix64(StageSeed ^ Mix64(DropletIndex)). Draw counter starts at
// zero for EACH droplet; Draw = Mix64(DropletSeed + counter++). Mix64 is the
// SplitMix64 finalizer including the 0x9E3779B97F4A7C15 initial addition.
// X is drawn before Y. For bound = cells+1, reject draws below (0-bound)%bound
// in uint64 arithmetic; accepted coordinate = draw%bound. At most 128 draws
// per coordinate; exhaustion reports an error, never biased fallback output.
// The version and vectors are provisional until the M4 random contract freezes.
constexpr uint32 AlgorithmVersion = 1;

// A caller may request any droplet independently. Coordinates include both
// endpoints [0,CellsX] x [0,CellsY]. Output is unchanged on error/cancellation.
VOXELLANDALGO_API FVoxelLandError Start(uint64 WorldSeed, const FGuid& StageId, uint32 Salt, uint32 DropletIndex,
	int32 CellsX, int32 CellsY, FIntPoint& OutStart, TFunctionRef<bool()> IsCancelled);

// Generates starts in index order ready for VoxelLandM4Erosion::Apply. Both
// allocation and work are capped (65536 droplets, 1024 cells per axis).
// Output is unchanged on error or cancellation.
VOXELLANDALGO_API FVoxelLandError Starts(uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 Count, int32 CellsX,
	int32 CellsY, TArray<FIntPoint>& OutStarts, TFunctionRef<bool()> IsCancelled);
}
