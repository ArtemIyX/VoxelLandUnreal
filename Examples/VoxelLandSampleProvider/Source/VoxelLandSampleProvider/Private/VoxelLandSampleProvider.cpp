#include "Modules/ModuleManager.h"
#include "Operators/VoxelLandOperator.h"
#include "VoxelLandSampleProviderId.h"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelLandSampleProvider, Log, All);

namespace
{
class FDoubleWorker final : public IVoxelLandOperatorWorker
{
public:
    bool Evaluate(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count, FString& Error) override
    {
        if (Inputs.Num() != 1 || !Inputs[0] || Count < 1 || Count > 256 || Output.Num() != Count)
        {
            Error = TEXT("Invalid double batch");
            return false;
        }
        const float* Input = static_cast<const float*>(Inputs[0]);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (!Output.WriteFloat(Index, Input[Index] * 2.0f))
            {
                Error = TEXT("Double result is not finite");
                return false;
            }
        }
        return true;
    }
};

class FDoubleOperator final : public IVoxelLandOperator
{
public:
    FGuid Id() const override { return VoxelLandSampleDoubleId(); }
    int32 Version() const override { return 1; }
    TArray<EVoxelLandGraphValueType> InputTypes() const override { return { EVoxelLandGraphValueType::Float }; }
    EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
    uint32 MaximumWorkerBytes() const override { return sizeof(FDoubleWorker); }
    bool ValidateConfiguration(TConstArrayView<uint8> Configuration, FString& Error) const override
    {
        if (!Configuration.IsEmpty())
        {
            Error = TEXT("Double takes no configuration");
            return false;
        }
        return true;
    }
    TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8> Configuration, uint64 Seed, FString& Error) const override
    {
        (void)Seed;
        if (!ValidateConfiguration(Configuration, Error))
        {
            return nullptr;
        }
        return MakeUnique<FDoubleWorker>();
    }
};
}

class FVoxelLandSampleProviderModule final : public IModuleInterface
{
public:
    bool SupportsDynamicReloading() override { return false; }

    void StartupModule() override
    {
        FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("VoxelLandAlgo"));
        FString Error;
        bRegistered = FVoxelLandOperatorRegistry::RegisterV2(FName(TEXT("VoxelLandSampleProvider")),
            MakeShared<FDoubleOperator, ESPMode::ThreadSafe>(), FVoxelLandOperatorRegistry::ProviderAbiVersion, Error);
        if (!bRegistered)
        {
            UE_LOG(LogVoxelLandSampleProvider, Error, TEXT("Could not register sample provider: %s"), *Error);
        }
    }

    void ShutdownModule() override
    {
        if (bRegistered)
        {
            FString Error;
            if (!FVoxelLandOperatorRegistry::Unregister(VoxelLandSampleDoubleId(), 1, Error))
            {
                UE_LOG(LogVoxelLandSampleProvider, Warning, TEXT("Could not unregister sample provider: %s"), *Error);
            }
            bRegistered = false;
        }
    }
private:
    bool bRegistered = false;
};

IMPLEMENT_MODULE(FVoxelLandSampleProviderModule, VoxelLandSampleProvider)
