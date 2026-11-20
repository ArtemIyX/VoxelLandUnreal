#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandExternalOperator.h"
#include "Data/Enums/VoxelLandOperatorSimdRequirement.h"
#include "Operators/VoxelLandOperatorOutput.h"

// Created, called, and destroyed exclusively on the same worker thread.
class VOXELLANDALGO_API IVoxelLandOperatorWorker
{
public:
    virtual ~IVoxelLandOperatorWorker() = default;
    // Inputs point to Count values; outputs use checked typed lane writes.
    // Bool input is int32 0 or 1. No view or writer outlives this call.
    virtual bool Evaluate(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count, FString& Error) = 0;
    // A translation-invariant one-cell filter reads quantized 37^3 halo samples in
    // x-major order and writes at most 256 samples from the 35^3 output lattice.
    // It must poll cancellation, never retain views, and return deterministic int16s.
    // Each result must depend only on its local neighborhood, configuration and
    // node seed; the output index locates the neighborhood but must not otherwise
    // affect the result. Call history, chunk order and position-dependent effects
    // are unsupported.
    virtual bool EvaluateFilter(TConstArrayView<int16> Halo, int32 Start, FVoxelLandOperatorOutput& Output,
        TFunctionRef<bool()> IsCancelled, FString& Error)
    {
        Error = TEXT("Filter kernel is unavailable");
        return false;
    }
    // Optional four-lane kernel; only called when the descriptor and CPU permit it.
    // Count is a positive multiple of four, with the same SoA layout and domains as Evaluate.
    virtual bool EvaluateSimd(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count, FString& Error)
    {
        (void)Inputs;
        (void)Output;
        (void)Count;
        Error = TEXT("SIMD kernel is unavailable");
        return false;
    }
};

// Providers are immutable after registration. Methods may be called concurrently.
class VOXELLANDALGO_API IVoxelLandOperator
{
public:
    virtual ~IVoxelLandOperator() = default;
    virtual EVoxelLandOperatorStage Stage() const { return EVoxelLandOperatorStage::Sample; }
    virtual FGuid Id() const = 0;
    virtual int32 Version() const = 0;
    virtual TArray<EVoxelLandGraphValueType> InputTypes() const = 0;
    virtual EVoxelLandGraphValueType OutputType() const = 0;
    virtual uint32 MaximumWorkerBytes() const = 0;
    // A provider must opt in and guarantee scalar-equivalent bitwise output and errors.
    virtual EVoxelLandOperatorSimdRequirement SimdRequirement() const { return EVoxelLandOperatorSimdRequirement::None; }
    virtual bool ValidateConfiguration(TConstArrayView<uint8> Configuration, FString& Error) const = 0;
    virtual TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8> Configuration,
        uint64 Seed, FString& Error) const = 0;
};

// Immutable provider leases captured atomically; safe to share during compile and validation.
class VOXELLANDALGO_API FVoxelLandOperatorRegistrySnapshot
{
public:
    TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Resolve(const FGuid& Id, int32 Version) const;
private:
    TArray<TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe>> Providers;
    friend class FVoxelLandOperatorRegistry;
};

// Registration pins the provider module binary until all program references are gone.
// Providers must not depend on module globals after shutdown.
// Modules must unregister successfully before unloading their provider code.
// Unregistration fails while a compiled program or worker retains the provider.
class VOXELLANDALGO_API FVoxelLandOperatorRegistry
{
public:
    static constexpr uint32 ProviderAbiVersion = 2;
    static FVoxelLandOperatorRegistrySnapshot Snapshot();
    // RegisterV2 is a new exported symbol: binaries using the raw-output ABI cannot bind.
    // Check the caller-compiled ABI value before invoking any provider virtual method.
    static bool RegisterV2(FName ModuleName, TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider,
        uint32 CallerAbiVersion, FString& Error);
    static bool Unregister(const FGuid& Id, int32 Version, FString& Error);
    static TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Resolve(const FGuid& Id, int32 Version);
};
