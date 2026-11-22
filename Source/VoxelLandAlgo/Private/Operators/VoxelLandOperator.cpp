#include "Operators/VoxelLandOperator.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

class FVoxelLandOperatorInternals final
{
public:
#if !IS_MONOLITHIC
	static void* AcquireModuleReference(const FString& Filename)
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
		FProviderLease(TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> InProvider, void* InHandle,
			const FGuid& InId, int32 InVersion, EVoxelLandOperatorStage InStage,
			EVoxelLandWorldRequirement InRequirement)
			: Provider(InProvider)
			, Handle(InHandle)
			, FrozenId(InId)
			, FrozenVersion(InVersion)
			, FrozenStage(InStage)
			, FrozenRequirement(InRequirement)
		{
		}

		virtual ~FProviderLease() override
		{
			Provider.Reset();
			if (Handle)
			{
				FPlatformProcess::FreeDllHandle(Handle);
			}
		}

		EVoxelLandOperatorStage Stage() const override { return FrozenStage; }
		EVoxelLandWorldRequirement WorldRequirement() const override { return FrozenRequirement; }
		FGuid Id() const override { return FrozenId; }
		int32 Version() const override { return FrozenVersion; }
		TArray<EVoxelLandGraphValueType> InputTypes() const override { return Provider->InputTypes(); }
		EVoxelLandGraphValueType OutputType() const override { return Provider->OutputType(); }
		uint32 MaximumWorkerBytes() const override { return Provider->MaximumWorkerBytes(); }
		EVoxelLandOperatorSimdRequirement SimdRequirement() const override { return Provider->SimdRequirement(); }

		bool ValidateConfiguration(TConstArrayView<uint8> Config, FString& Error) const override
		{
			return Provider->ValidateConfiguration(Config, Error);
		}

		TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(
			TConstArrayView<uint8> Config, uint64 Seed, FString& Error) const override
		{
			return Provider->CreateWorker(Config, Seed, Error);
		}

	private:
		TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider;
		void* Handle = nullptr;
		const FGuid FrozenId;
		const int32 FrozenVersion;
		const EVoxelLandOperatorStage FrozenStage;
		const EVoxelLandWorldRequirement FrozenRequirement;
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

	static FRegistryState& Registry()
	{
		static FRegistryState* State = new FRegistryState();
		return *State;
	}
};

bool FVoxelLandOperatorRegistry::RegisterV3(FName ModuleName,
	TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider, uint32 CallerAbiVersion, FString& Error)
{
	Error.Reset();
	if (CallerAbiVersion != ProviderAbiVersion)
	{
		Error = TEXT("Incompatible VoxelLand C++ provider ABI; rebuild the provider against this plugin version");
		return false;
	}
	const FGuid OperatorId = Provider->Id();
	const FGuid ReservedPrefix = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	if (OperatorId.A == ReservedPrefix.A && OperatorId.B == ReservedPrefix.B && OperatorId.C == ReservedPrefix.C)
	{
		Error = TEXT("Operator ID uses the reserved VoxelLand builtin and function namespace");
		return false;
	}
	const int32 OperatorVersion = Provider->Version();
	const EVoxelLandOperatorStage OperatorStage = Provider->Stage();
	const EVoxelLandWorldRequirement Requirement = Provider->WorldRequirement();
	const TArray<EVoxelLandGraphValueType> Inputs = Provider->InputTypes();
	const EVoxelLandOperatorSimdRequirement Simd = Provider->SimdRequirement();
	if (!OperatorId.IsValid() || OperatorVersion < 1 || Inputs.Num() < 1 || Inputs.Num() > 3
		|| (OperatorStage != EVoxelLandOperatorStage::Sample && OperatorStage != EVoxelLandOperatorStage::OneCellFilter)
		|| (OperatorStage == EVoxelLandOperatorStage::OneCellFilter
			&& (Inputs.Num() != 1 || Inputs[0] != EVoxelLandGraphValueType::Float
				|| Provider->OutputType() != EVoxelLandGraphValueType::Float
				|| Simd != EVoxelLandOperatorSimdRequirement::None))
		|| (Simd != EVoxelLandOperatorSimdRequirement::None && Simd != EVoxelLandOperatorSimdRequirement::SSE2)
		|| (Requirement != EVoxelLandWorldRequirement::Unspecified
			&& Requirement != EVoxelLandWorldRequirement::SampleLocal
			&& Requirement != EVoxelLandWorldRequirement::BoundedNeighborhood
			&& Requirement != EVoxelLandWorldRequirement::BoundedRegion
			&& Requirement != EVoxelLandWorldRequirement::FiniteGlobal)
		|| (OperatorStage == EVoxelLandOperatorStage::OneCellFilter
			&& Requirement == EVoxelLandWorldRequirement::SampleLocal))
	{
		Error = TEXT("Invalid operator ID, version or signature");
		return false;
	}
	const auto IsType = [](EVoxelLandGraphValueType Type) {
		return Type == EVoxelLandGraphValueType::Float || Type == EVoxelLandGraphValueType::Int32
			|| Type == EVoxelLandGraphValueType::Bool;
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
	if (Filename.IsEmpty() || !(Handle = FVoxelLandOperatorInternals::AcquireModuleReference(Filename)))
	{
		Error = TEXT("Cannot pin operator provider module binary");
		return false;
	}
#endif
	TSharedRef<const IVoxelLandOperator, ESPMode::ThreadSafe> Lease =
		MakeShared<FVoxelLandOperatorInternals::FProviderLease, ESPMode::ThreadSafe>(
			Provider, Handle, OperatorId, OperatorVersion, OperatorStage, Requirement);
	FVoxelLandOperatorInternals::FRegistryState& State = FVoxelLandOperatorInternals::Registry();
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
		State.FrameworkHandle =
			FrameworkPath.IsEmpty() ? nullptr : FVoxelLandOperatorInternals::AcquireModuleReference(FrameworkPath);
		if (!State.FrameworkHandle)
		{
			Error = TEXT("Cannot pin VoxelLand operator framework");
			return false;
		}
	}
#endif
	for (const auto& Existing : State.Providers)
	{
		if (Existing->Id() == OperatorId && Existing->Version() == OperatorVersion)
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
	FVoxelLandOperatorInternals::FRegistryState& State = FVoxelLandOperatorInternals::Registry();
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

TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> FVoxelLandOperatorRegistry::Resolve(
	const FGuid& Id, int32 Version)
{
	FVoxelLandOperatorInternals::FRegistryState& State = FVoxelLandOperatorInternals::Registry();
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

FVoxelLandOperatorRegistrySnapshot FVoxelLandOperatorRegistry::Snapshot()
{
	FVoxelLandOperatorInternals::FRegistryState& State = FVoxelLandOperatorInternals::Registry();
	FScopeLock Lock(&State.Lock);
	FVoxelLandOperatorRegistrySnapshot Result;
	Result.Providers = State.Providers;
	return Result;
}

TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> FVoxelLandOperatorRegistrySnapshot::Resolve(
	const FGuid& Id, int32 Version) const
{
	for (const auto& Provider : Providers)
	{
		if (Provider->Id() == Id && Provider->Version() == Version)
		{
			return Provider;
		}
	}
	return nullptr;
}