#include "Operators/VoxelLandOperator.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
#if !IS_MONOLITHIC
void* AcquireModuleReference(const FString& Filename)
{
#if PLATFORM_WINDOWS
    // UE 5.7 GetDllHandle can return a borrowed GetModuleHandle result for
    // already-loaded DLLs. LoadLibraryW takes a new OS reference instead.
    const FString Absolute = FPaths::ConvertRelativePathToFull(Filename);
    return reinterpret_cast<void*>(::LoadLibraryW(*Absolute));
#elif PLATFORM_LINUX
    // UnixPlatformProcess uses dlopen, which takes a reference balanced by dlclose.
    return FPlatformProcess::GetDllHandle(*Filename);
#else
    // A platform without known owning-handle semantics must not fabricate a pin.
    return nullptr;
#endif
}
#endif

class FProviderLease final : public IVoxelLandOperator
{
public:
    FProviderLease(TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> InProvider, void* InHandle)
        : Provider(InProvider), Handle(InHandle) {}
    virtual ~FProviderLease() override
    {
        Provider.Reset();
        if (Handle)
        {
            FPlatformProcess::FreeDllHandle(Handle);
        }
    }
    FGuid Id() const override { return Provider->Id(); }
    int32 Version() const override { return Provider->Version(); }
    TArray<EVoxelLandGraphValueType> InputTypes() const override { return Provider->InputTypes(); }
    EVoxelLandGraphValueType OutputType() const override { return Provider->OutputType(); }
    uint32 MaximumWorkerBytes() const override { return Provider->MaximumWorkerBytes(); }
    EVoxelLandOperatorSimdRequirement SimdRequirement() const override { return Provider->SimdRequirement(); }
    bool ValidateConfiguration(TConstArrayView<uint8> Config, FString& Error) const override
    { return Provider->ValidateConfiguration(Config, Error); }
    TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8> Config, uint64 Seed, FString& Error) const override
    { return Provider->CreateWorker(Config, Seed, Error); }
private:
    TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider;
    void* Handle = nullptr;
};
// Registry state is process-lifetime. A live program may release its final provider on
// any worker after module shutdown. Never destroy the state or execute its lease code
// from an already-unloaded module during static teardown.
class FRegistryState
{
public:
    FCriticalSection Lock;
    TArray<TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe>> Providers;
    void* FrameworkHandle = nullptr;
};
FRegistryState& Registry()
{
    static FRegistryState* State = new FRegistryState();
    return *State;
}
}

bool FVoxelLandOperatorRegistry::Register(FName ModuleName, TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider, FString& Error)
{
    Error.Reset();
    const TArray<EVoxelLandGraphValueType> Inputs = Provider->InputTypes();
    const EVoxelLandOperatorSimdRequirement Simd = Provider->SimdRequirement();
    if (!Provider->Id().IsValid() || Provider->Version() < 1 || Inputs.Num() < 1 || Inputs.Num() > 3 ||
        (Simd != EVoxelLandOperatorSimdRequirement::None && Simd != EVoxelLandOperatorSimdRequirement::SSE2))
    {
        Error = TEXT("Invalid operator ID, version or signature");
        return false;
    }
    const auto IsType = [](EVoxelLandGraphValueType Type) {
        return Type == EVoxelLandGraphValueType::Float || Type == EVoxelLandGraphValueType::Int32 ||
            Type == EVoxelLandGraphValueType::Bool;
    };
    if (!IsType(Provider->OutputType()))
    {
        Error = TEXT("Invalid operator output type");
        return false;
    }
    for (EVoxelLandGraphValueType Type : Inputs)
    {
        if (!IsType(Type))
        {
            Error = TEXT("Invalid operator input type");
            return false;
        }
    }
    void* Handle = nullptr;
#if !IS_MONOLITHIC
    if (ModuleName.IsNone() || !FModuleManager::Get().IsModuleLoaded(ModuleName))
    {
        Error = TEXT("Operator provider module is not loaded");
        return false;
    }
    const FString Filename = FModuleManager::Get().GetModuleFilename(ModuleName);
    if (Filename.IsEmpty() || !(Handle = AcquireModuleReference(Filename)))
    {
        Error = TEXT("Cannot pin operator provider module binary");
        return false;
    }
#endif
    TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Lease =
        MakeShared<FProviderLease, ESPMode::ThreadSafe>(Provider, Handle);
    FRegistryState& State = Registry();
    FScopeLock Lock(&State.Lock);
#if !IS_MONOLITHIC
    if (!State.FrameworkHandle)
    {
        const FName FrameworkName(TEXT("VoxelLandAlgo"));
        if (!FModuleManager::Get().IsModuleLoaded(FrameworkName))
        {
            Error = TEXT("VoxelLand operator framework is not loaded");
            return false;
        }
        const FString FrameworkPath = FModuleManager::Get().GetModuleFilename(FrameworkName);
        State.FrameworkHandle = FrameworkPath.IsEmpty() ? nullptr : AcquireModuleReference(FrameworkPath);
        if (!State.FrameworkHandle)
        {
            Error = TEXT("Cannot pin VoxelLand operator framework");
            return false;
        }
    }
#endif
    for (const auto& Existing : State.Providers)
    {
        if (Existing->Id() == Provider->Id() && Existing->Version() == Provider->Version())
        {
            Error = TEXT("Duplicate operator ID and version");
            return false;
        }
    }
    State.Providers.Add(Lease);
    return true;
}

bool FVoxelLandOperatorRegistry::Unregister(const FGuid& Id, int32 Version, FString& Error)
{
    FRegistryState& State = Registry();
    FScopeLock Lock(&State.Lock);
    Error.Reset();
    for (int32 Index = 0; Index < State.Providers.Num(); ++Index)
    {
        if (State.Providers[Index]->Id() == Id && State.Providers[Index]->Version() == Version)
        {
            if (!State.Providers[Index].IsUnique())
            {
                Error = TEXT("Operator remains in use by a program or worker");
                return false;
            }
            State.Providers.RemoveAt(Index);
            return true;
        }
    }
    Error = TEXT("Operator is not registered");
    return false;
}

TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> FVoxelLandOperatorRegistry::Resolve(const FGuid& Id, int32 Version)
{
    FRegistryState& State = Registry();
    FScopeLock Lock(&State.Lock);
    for (const auto& Provider : State.Providers)
    {
        if (Provider->Id() == Id && Provider->Version() == Version)
        {
            return Provider;
        }
    }
    return nullptr;
}
