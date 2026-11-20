#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandProgramDraft.h"
#include "Data/Structs/VoxelLandZoneProfile.h"
#include "Generation/VoxelLandGeneratorSettings.h"

class IVoxelLandOperator;
class FVoxelLandWorkerContext;
struct FVoxelLandWorkerContextImpl;

// Copies and validates the draft before publishing it. Safe to share across workers.
class VOXELLANDALGO_API FVoxelLandProgram
{
public:
	static FVoxelLandError Validate(
		const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	const FVoxelLandProgramDraft& GetData() const { return Data; }
	uint64 GetFingerprint() const { return Fingerprint; }
	const TArray<TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe>>& GetOperators() const { return Operators; }

private:
	FVoxelLandProgram() = default;
	FVoxelLandProgram(const FVoxelLandProgram&) = delete;
	FVoxelLandProgram& operator=(const FVoxelLandProgram&) = delete;
	FVoxelLandProgramDraft Data;
	TArray<TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe>> Operators;
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
	FVoxelLandError GenerateChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	// Explicit filter API. Compiled filter stages are also applied by GenerateChunk.
	// Strength maps to uint16 by rounding Strength * 65535 to nearest; integer averages
	// and blends round to nearest, ties away from zero. The source has a one-cell halo.
	FVoxelLandError GenerateSmoothedChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		bool bEnabled, float Strength, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
		TFunctionRef<bool()> IsCancelled) const;
	// Evaluate the pure VM stage at 1..256 positions; neighborhood filters apply only to chunks.
	FVoxelLandError EvaluateSamples(
		TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity) const;
	const FVoxelLandZoneProfile& GetProfile() const { return Profile; }
	const FVoxelLandProgram& GetProgram() const { return *Program; }

private:
	FVoxelLandError GeneratePureChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandGenerator() = default;
	FVoxelLandGenerator(const FVoxelLandGenerator&) = delete;
	FVoxelLandGenerator& operator=(const FVoxelLandGenerator&) = delete;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	FVoxelLandZoneProfile Profile;
	FVoxelLandBackendOptions Options;
	bool bSmoothEnabled = false;
	float SmoothStrength = 0.f;
	TArray<float> FloatParameters;
	TArray<int32> IntParameters;
	TArray<int32> Seeds;
	TArray<uint64> FullSeeds;
	friend class FVoxelLandWorkerContext;
	friend struct FVoxelLandWorkerContextImpl;
};

// Exclusive to its creating worker, including destruction; buffers are reused across chunks.
class VOXELLANDALGO_API FVoxelLandWorkerContext
{
public:
	~FVoxelLandWorkerContext();
	EVoxelLandVMBackend GetVMBackend() const;
	EVoxelLandNoiseBackend GetNoiseBackend() const;

private:
	TUniquePtr<FVoxelLandWorkerContextImpl> Impl;
	TArray<int16> SmoothScratch;
	FVoxelLandWorkerContext();
	friend class FVoxelLandGenerator;
};

namespace VoxelLandDeterminism
{
VOXELLANDALGO_API uint64 DeriveNodeSeed(uint64 WorldSeed, const FGuid& NodeId, uint32 Salt);
VOXELLANDALGO_API FVoxelLandError Quantize(float DistanceCm, float VoxelSizeCm, int16& OutDensity);
VOXELLANDALGO_API uint64 HashDensity(const TArray<int16>& Density);
}
