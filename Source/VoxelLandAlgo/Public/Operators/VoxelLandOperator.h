#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandExternalOperator.h"
#include "Data/Enums/VoxelLandOperatorSimdRequirement.h"

// Created, called, and destroyed exclusively on the same worker thread.
class VOXELLANDALGO_API IVoxelLandOperatorWorker
{
public:
    virtual ~IVoxelLandOperatorWorker() = default;
    // Inputs and output point to arrays of Count float or int32 values according to the signature.
    // Bool is represented strictly as int32 0 or 1. No array outlives this call.
    virtual bool Evaluate(TConstArrayView<const void*> Inputs, void* Output, int32 Count, FString& Error) = 0;
    // Optional four-lane kernel; only called when the descriptor and CPU permit it.
    // Count is a positive multiple of four, with the same SoA layout and domains as Evaluate.
    virtual bool EvaluateSimd(TConstArrayView<const void*> Inputs, void* Output, int32 Count, FString& Error)
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

// Registration pins the provider module binary until all program references are gone.
// Providers must not depend on module globals after shutdown.
// Modules must unregister successfully before unloading their provider code.
// Unregistration fails while a compiled program or worker retains the provider.
class VOXELLANDALGO_API FVoxelLandOperatorRegistry
{
public:
    static bool Register(FName ModuleName, TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider, FString& Error);
    static bool Unregister(const FGuid& Id, int32 Version, FString& Error);
    static TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Resolve(const FGuid& Id, int32 Version);
};
