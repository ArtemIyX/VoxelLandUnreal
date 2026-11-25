#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Execution/VoxelLandBackendOptions.h"
#include "Data/Field/Chunks/VoxelLandChunkClassification.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Program/Execution/VoxelLandProgramDraft.h"
#include "Data/World/Configuration/VoxelLandZoneProfile.h"
#include "Generation/Field/VoxelLandGeneratorSettings.h"
#include "Data/Terrain/Shaping/VoxelLandOverhangConfig.h"
#include "Data/Terrain/Shaping/VoxelLandHeightDetailConfig.h"

class IVoxelLandOperator;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandOperatorRegistrySnapshot;
class FVoxelLandWorkerContext;
class FVoxelLandM4HeightGrid;
struct FVoxelLandM4UnlimitedConfig;
struct FVoxelLandM4SmoothSettings;
struct FVoxelLandM5PointCm;
struct FVoxelLandM6FilterPlan;
struct FVoxelLandM7CompletedBox;
struct FVoxelLandM7SampleGrid;
struct FVoxelLandTerrainHeightColumns;
struct FVoxelLandM7NormalAnchor;
struct FVoxelLandM7NormalFrameBatch;
struct FVoxelLandWorkerContextImpl;

// Copies and validates the draft before publishing it. Safe to share across workers.
class VOXELLANDALGO_API FVoxelLandProgram
{
public:
	static FVoxelLandError Validate(
		const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	static FVoxelLandError Validate(const FVoxelLandProgramDraft& Draft,
		const FVoxelLandOperatorRegistrySnapshot& Providers,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	// Additive M5 path: rejects external providers until they have a bounded API.
	// On failure OutProgram is unchanged; the validated program retains its memory lease.
	static FVoxelLandM5Diagnostic ValidateMetered(const FVoxelLandProgramDraft& Draft,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	const FVoxelLandProgramDraft& GetData() const { return Data; }
	uint64 GetFingerprint() const { return Fingerprint; }
	const TArray<TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe>>& GetOperators() const { return Operators; }

private:
	friend class FVoxelLandGenerator;
	static FVoxelLandError ValidateInternal(const FVoxelLandProgramDraft& Draft,
		const FVoxelLandOperatorRegistrySnapshot& Providers, TFunctionRef<bool()> IsCancelled, bool bMetered,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease,
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
	FVoxelLandProgram() = default;
	FVoxelLandProgram(const FVoxelLandProgram&) = delete;
	FVoxelLandProgram& operator=(const FVoxelLandProgram&) = delete;
	// Declared first so the retained allocation lease is released after Data.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> M5Lease;
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
	// Worker thread only. The program must be metered against Budget and all
	// settings must remain immutable during the call. This admits logical setup
	// allocations/work only; workers, native providers, input storage and allocator
	// overhead require separate admission. Failure or cancellation preserves output.
	static FVoxelLandM5Diagnostic CreateMetered(const FVoxelLandGeneratorSettings& Settings,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Finite M4 setup using the same metered program and generator admission.
	// The caller independently admits the grid and retains its provenance/lease;
	// workers, providers and allocator overhead also require separate admission.
	// Worker thread only; failure or cancellation preserves OutGenerator. This
	// does not certify terrain or authorize world publication.
	static FVoxelLandM5Diagnostic CreateM4HeightFieldMetered(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
		const FVoxelLandM4HeightDetailConfig& Detail, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Transitional explicit height-field source. The VM's pure code is not run;
	// only its optional built-in Smooth Density stage is retained. External operators and filters
	// are not supported. Existing Create is unchanged.
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Explicit, independently seeded 3D Perlin detail; disabled is bit-identical.
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Explicit final filter independent of the control graph. Its version is validated.
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4HeightDetailConfig& Detail,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	static FVoxelLandError CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
		const FVoxelLandM4HeightDetailConfig& Detail,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// M4 without global erosion; bounded by the VM's exact integer coordinate range.
	static FVoxelLandError CreateM4Unlimited(const FVoxelLandGeneratorSettings& Settings,
		const FVoxelLandM4UnlimitedConfig& Config,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	// Finite M4 only. MinLattice and Size describe an X-fastest LOD0 box.
	// Caller retains output allocation admission; this call meters temporary
	// allocations/work and requires the exact thread-affine worker. Failure
	// and cancellation preserve OutDensity. Native provider resources are separate.
	FVoxelLandM5Diagnostic GenerateFiniteM4Box(const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
		FVoxelLandWorkerContext& Worker, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TArray<int16>& OutDensity) const;
	FVoxelLandError CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const;
	// Checks exact generator identity and creating thread without allocating.
	bool OwnsWorkerOnCurrentThread(const FVoxelLandWorkerContext& Worker) const;
	// Proof-only hint; chunk sampling still runs and remains authoritative.
	EVoxelLandChunkClassification ClassifyChunkBeforeSampling(const FVoxelLandChunkKey& Key) const;
	// Thread-safe demand gate. LOD0 checks its complete admitted footprint.
	// Coarse demand checks the baseline hull; the worker validates exact M6 tiles.
	FVoxelLandError ValidateM4ChunkDemand(const FVoxelLandChunkKey& Key) const;
	FVoxelLandError GenerateChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk) const;
	FVoxelLandError GenerateChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	// Samples canonical shared frames from the completed M4/M6 field on the
	// owner worker. Errors preserve the caller's batch.
	FVoxelLandError GenerateM7NormalFrames(TConstArrayView<FVoxelLandM7NormalAnchor> Anchors,
		FVoxelLandWorkerContext& Worker, FVoxelLandM7NormalFrameBatch& OutBatch,
		TFunctionRef<bool()> IsCancelled) const;
	// Explicit compatibility-filter API. Use GenerateChunk for the configured
	// M6 region pipeline. Strength maps to uint16 by rounding Strength * 65535 to nearest; integer averages
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
	bool IsM4Unlimited() const { return M4Unlimited.IsValid(); }
	// Coarse render sampling supports ordinary finite and erosion-disabled local worlds.
	bool SupportsM7CompletedField() const;
	const FVoxelLandProgram& GetProgram() const { return *Program; }
	// This payload supplements the baseline generator inputs. An inactive stage has no bytes.
	TConstArrayView<uint8> GetM6StampIdentity() const { return M6StampIdentity; }
	TConstArrayView<uint8> GetM6FilterIdentity() const { return M6FilterIdentity; }
	TConstArrayView<uint8> GetM6RedistanceIdentity() const { return M6RedistanceIdentity; }
	TConstArrayView<uint8> GetM6SphereIdentity() const
	{
		return bM6LegacySphereEnvelope ? GetM6StampIdentity() : TConstArrayView<uint8>();
	}
	// Immutable resolved values used by the versioned fixed-lattice control backend.
	TConstArrayView<float> GetResolvedFloatParameters() const { return FloatParameters; }
	TConstArrayView<int32> GetResolvedIntParameters() const { return IntParameters; }
	TConstArrayView<uint64> GetResolvedSeeds() const { return FullSeeds; }
	// Finite M4 height grids remain immutable and retain their resource lease.
	// Returns null for M3 and chunk-local M4 generators.
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> GetM4HeightGrid() const { return M4HeightGrid; }

private:
	static FVoxelLandM5Diagnostic CreateMeteredInternal(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig* Overhang, const FVoxelLandM4SmoothSettings* Smooth,
		const FVoxelLandM4HeightDetailConfig* Detail, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator);
	static FVoxelLandError CreateInternal(const FVoxelLandGeneratorSettings& Settings,
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
		const FVoxelLandM4OverhangConfig* Overhang,
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
		TFunctionRef<bool()>* IsCancelled = nullptr);
	FVoxelLandError GenerateM4Density(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset,
		FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled) const;
	// Caller validates lattice range, thread ownership and dimensions (1..37).
	FVoxelLandError GenerateM4DensityBox(const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
		FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError SampleTerrainHeightColumns(const FVoxelLandM7SampleGrid& Grid,
		FVoxelLandTerrainHeightColumns& Columns, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError ApplyTerrainHeightDetail(const FVoxelLandM7SampleGrid& Grid,
		FVoxelLandTerrainHeightColumns& Columns, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError WriteTerrainDensityGrid(const FVoxelLandM7SampleGrid& Grid,
		const FVoxelLandTerrainHeightColumns& Columns, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity,
		TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateM4SampleGrid(const FVoxelLandM7SampleGrid& Grid, FVoxelLandWorkerContext& Worker,
		TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight = nullptr) const;
	FVoxelLandError GenerateM4StridedDetail(const FVoxelLandM7SampleGrid& Grid, const FIntVector& BaseOffset,
		FVoxelLandWorkerContext& Worker, TArray<int32>& Heights, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	bool SampleM4BaseHeight(
		int64 X, int64 Y, int32& Height, TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError ValidateM4Footprint(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset) const;
	FVoxelLandError EvaluateToWorkerBuffer(TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker) const;
	FVoxelLandError GenerateExpandedDensity(
		const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateExternalFilteredChunkCancellable(const FVoxelLandChunkKey& Key,
		FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
		TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GeneratePureChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError InitializeM6Spheres(const FVoxelLandM6SphereSettings& Settings);
	FVoxelLandError InitializeM6Stamps(const FVoxelLandM6StampSettings& Settings, float VoxelSizeCm);
	FVoxelLandError ValidateM6SphereFootprint(const FVoxelLandChunkKey& Key, int32 Halo = 0) const;
	FVoxelLandError ApplyM6Stamps(const FInt64Vector& FirstSampleCm, int32 Axis, TArray<int16>& Density,
		TFunctionRef<bool()> IsCancelled, int32 Stride = 1) const;
	FVoxelLandError InitializeM6Filters(const FVoxelLandM6FilterSettings& Settings, float VoxelSizeCm);
	FVoxelLandError InitializeM6Redistance(const FVoxelLandM6RedistanceSettings& Settings, float VoxelSizeCm);
	FVoxelLandM6FilterPlan MakeM6FilterPlan(const FVoxelLandChunkKey& Key) const;
	FVoxelLandM6FilterPlan MakeM6FilterPlan(const FInt64Vector& StoredMin, int32 StoredAxis) const;
	FVoxelLandError ApplyM6Box(const FInt64Vector& StoredFirst, int32 StoredAxis, const FVoxelLandM6FilterPlan& Plan,
		TArray<int16>& Density, TFunctionRef<bool()> IsCancelled, int32 Stride = 1) const;
	FVoxelLandError ValidateM6FilterDemand(const FVoxelLandChunkKey& Key, const FVoxelLandM6FilterPlan& Plan) const;
	int32 GetM6CompatibilityHalo() const;
	FVoxelLandError ValidateM6BaselineFootprint(const FVoxelLandChunkKey& Key, int32 Halo) const;
	FVoxelLandError GenerateM6BaselineBox(const FVoxelLandChunkKey& Key, int32 Halo, FVoxelLandWorkerContext& Worker,
		TArray<int16>& Density, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateM4TiledBaselineBox(const FInt64Vector& MinLattice, int32 Axis,
		FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight = nullptr) const;
	bool SupportsM7FiniteSource() const;
	FVoxelLandError GenerateM7StridedBaseline(const FVoxelLandM7SampleGrid& Grid, FVoxelLandWorkerContext& Worker,
		TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError ApplyM7CoarsePatches(const FVoxelLandM7SampleGrid& Grid, int32 GroupSize, bool bPlanOnly,
		FVoxelLandWorkerContext& Worker, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError ValidateM7CoarseDemand(const FVoxelLandChunkKey& Key) const;
	FVoxelLandError GenerateM7CoarseChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError ValidateM7RawFootprint(const FInt64Vector& First, const FInt64Vector& Last) const;
	FVoxelLandError ValidateM7StampBox(const FInt64Vector& First, const FInt64Vector& Last) const;
	FVoxelLandError PlanM7CompletedBox(const FInt64Vector& First, int32 Axis, FVoxelLandM7CompletedBox& Out) const;
	FVoxelLandError ReadM7CompletedBox(const FVoxelLandM7CompletedBox& Box, FVoxelLandWorkerContext& Worker,
		TArray<int16>& Out, TFunctionRef<bool()> IsCancelled,
		TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const;
	FVoxelLandError ValidateM7FiniteBox(const FInt64Vector& First, int32 Axis) const;
	FVoxelLandError ValidateM7FiniteDemand(const FVoxelLandChunkKey& Key) const;
	FVoxelLandError GenerateM7FiniteChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateM6LocalBaselineBox(
		const FVoxelLandChunkKey& Key, int32 Halo, TArray<int16>& Density, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateM6ProcessedChunk(const FVoxelLandChunkKey& Key, const FVoxelLandM6FilterPlan& Plan,
		FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
		TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError GenerateM4LocalChunkWithStamps(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const;
	FVoxelLandError FinalizeDensityChunk(TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk,
		FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
		TFunctionRef<bool()> IsCancelled, bool bApplyStamps = true) const;
	FVoxelLandGenerator() = default;
	FVoxelLandGenerator(const FVoxelLandGenerator&) = delete;
	FVoxelLandGenerator& operator=(const FVoxelLandGenerator&) = delete;
	// Declared first so its admission outlives all generator-owned allocations.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> M5Lease;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> M4HeightGrid;
	TSharedPtr<const FVoxelLandM4UnlimitedConfig, ESPMode::ThreadSafe> M4Unlimited;
	TSharedPtr<const FVoxelLandGeneratorSettings, ESPMode::ThreadSafe> M4UnlimitedSettings;
	bool bM4NormalizeSlope = false;
	FVoxelLandM4OverhangConfig M4Overhang;
	int32 M4OverhangSeed = 0;
	bool bM4FixedLatticeShaping = false;
	FVoxelLandM4HeightDetailConfig M4HeightDetail;
	int32 M4HeightDetailSeed = 0;
	FVoxelLandZoneProfile Profile;
	FVoxelLandBackendOptions Options;
	bool bSmoothEnabled = false;
	float SmoothStrength = 0.f;
	TArray<float> FloatParameters;
	TArray<int32> IntParameters;
	TArray<int32> Seeds;
	TArray<uint64> FullSeeds;
	TArray<FVoxelLandM6Stamp> M6Stamps;
	TArray<uint8> M6StampIdentity;
	TArray<FVoxelLandM6FilterStage> M6Filters;
	TArray<uint8> M6FilterIdentity;
	TArray<uint8> M6RedistanceIdentity;
	uint8 M6RedistanceBand = 0;
	bool bM6LegacySphereEnvelope = false;
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
