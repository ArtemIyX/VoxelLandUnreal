#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Enums/VoxelLandChunkClassification.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandProgramDraft.h"
#include "Data/Structs/VoxelLandZoneProfile.h"
#include "Generation/VoxelLandGeneratorSettings.h"
#include "Data/Structs/VoxelLandM4OverhangConfig.h"

class IVoxelLandOperator;
class FVoxelLandOperatorRegistrySnapshot;
class FVoxelLandWorkerContext;
class FVoxelLandM4HeightGrid;
struct FVoxelLandWorkerContextImpl;

// Copies and validates the draft before publishing it. Safe to share across workers.
class VOXELLANDALGO_API FVoxelLandProgram
{
public:
	static FVoxelLandError Validate(
		const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	static FVoxelLandError Validate(const FVoxelLandProgramDraft& Draft, const FVoxelLandOperatorRegistrySnapshot& Providers,
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
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
	// Transitional explicit height-field source. The VM's pure code is not run;
	// only its optional built-in Smooth Density stage is retained. External operators and filters,
	// height detail and overhangs are not supported. Existing Create is unchanged.
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Explicit, independently seeded 3D Perlin detail; disabled is bit-identical.
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	FVoxelLandError CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const;
    // Proof-only hint; chunk sampling still runs and remains authoritative.
    EVoxelLandChunkClassification ClassifyChunkBeforeSampling(const FVoxelLandChunkKey& Key) const;
	// Thread-safe read-only demand gate. M3 returns success and retains its
	// existing scheduler validation; M4 checks padded/filter/stencil XY and Z.
	FVoxelLandError ValidateM4ChunkDemand(const FVoxelLandChunkKey& Key) const;
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
	// Raw finite values from the same pure VM stage before density quantization and filters.
	FVoxelLandError EvaluateSamplesRaw(
		TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<float>& OutValues) const;
	const FVoxelLandZoneProfile& GetProfile() const { return Profile; }
	const FVoxelLandProgram& GetProgram() const { return *Program; }

private:
	static FVoxelLandError CreateInternal(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig* Overhang,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	FVoxelLandError GenerateM4Density(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset,
		FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError ValidateM4Footprint(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset) const;
	FVoxelLandError EvaluateToWorkerBuffer(TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker) const;
	FVoxelLandError GenerateExpandedDensity(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateExternalFilteredChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
        TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GeneratePureChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandGenerator() = default;
	FVoxelLandGenerator(const FVoxelLandGenerator&) = delete;
	FVoxelLandGenerator& operator=(const FVoxelLandGenerator&) = delete;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> M4HeightGrid;
	bool bM4NormalizeSlope = false;
	FVoxelLandM4OverhangConfig M4Overhang;
	int32 M4OverhangSeed = 0;
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
