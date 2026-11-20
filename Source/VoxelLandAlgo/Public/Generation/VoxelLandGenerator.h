#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandProgramDraft.h"
#include "Data/Structs/VoxelLandZoneProfile.h"
#include "Generation/VoxelLandGeneratorSettings.h"

class FVoxelLandWorkerContext;

// Copies and validates the draft before publishing it. Safe to share across workers.
class VOXELLANDALGO_API FVoxelLandProgram
{
public:
	static FVoxelLandError Validate(
		const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	const FVoxelLandProgramDraft& GetData() const { return Data; }
	uint64 GetFingerprint() const { return Fingerprint; }

private:
	FVoxelLandProgram() = default;
	FVoxelLandProgram(const FVoxelLandProgram&) = delete;
	FVoxelLandProgram& operator=(const FVoxelLandProgram&) = delete;
	FVoxelLandProgramDraft Data;
	uint64 Fingerprint = 0;
};

// Immutable snapshot. Create and all worker operations must be called off the game thread.
class VOXELLANDALGO_API FVoxelLandGenerator : public TSharedFromThis<FVoxelLandGenerator, ESPMode::ThreadSafe>
{
public:
	static FVoxelLandError Create(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	FVoxelLandError CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const;
	FVoxelLandError GenerateChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk) const;
	// Evaluate 1..256 arbitrary world-space positions using the same VM and quantization as chunks.
	FVoxelLandError EvaluateSamples(
		TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity) const;
	const FVoxelLandZoneProfile& GetProfile() const { return Profile; }
	const FVoxelLandProgram& GetProgram() const { return *Program; }

private:
	FVoxelLandGenerator() = default;
	FVoxelLandGenerator(const FVoxelLandGenerator&) = delete;
	FVoxelLandGenerator& operator=(const FVoxelLandGenerator&) = delete;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	FVoxelLandZoneProfile Profile;
	FVoxelLandBackendOptions Options;
	TArray<float> FloatParameters;
	TArray<int32> IntParameters;
	TArray<int32> Seeds;
	friend class FVoxelLandWorkerContext;
};

// Exclusive to its creating worker, including destruction; buffers are reused across chunks.
class VOXELLANDALGO_API FVoxelLandWorkerContext
{
public:
	~FVoxelLandWorkerContext();
	EVoxelLandVMBackend GetVMBackend() const;
	EVoxelLandNoiseBackend GetNoiseBackend() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	FVoxelLandWorkerContext();
	friend class FVoxelLandGenerator;
};

namespace VoxelLandDeterminism
{
VOXELLANDALGO_API uint64 DeriveNodeSeed(uint64 WorldSeed, const FGuid& NodeId, uint32 Salt);
VOXELLANDALGO_API FVoxelLandError Quantize(float DistanceCm, float VoxelSizeCm, int16& OutDensity);
VOXELLANDALGO_API uint64 HashDensity(const TArray<int16>& Density);
}
