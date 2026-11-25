#include "World/VoxelLandWorld.h"
#include "World/VoxelLandM4Preparation.h"
#include "World/VoxelLandM4PrepassCoordinator.h"
#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM4OverrideValidation.h"
#include "World/VoxelLandM4CurveValidation.h"
#include "World/VoxelLandM4SavedWorldPreflight.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandFiniteSnapshotAdapter.h"

#include "Async/Async.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Data/Structs/VoxelLandReloadTestBarrier.h"
#endif
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Structs/VoxelLandWorldSourceSnapshot.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandM6WorldConfig.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldDefinition.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Data/Structs/VoxelLandM4OverhangConfig.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Data/Structs/VoxelLandWorldState.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlPipeline.h"
#include "Generation/VoxelLandM4ControlPipelineInputs.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM6OverlayValidation.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Graph/VoxelLandProgramCodec.h"
#if WITH_EDITOR
#include "UObject/PackageReload.h"
#endif
#if !UE_SERVER
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#endif

class FVoxelLandWorldSettingsInitInternals final
{
public:
#if !UE_SERVER

#endif
	static void CopyOverrides(const UVoxelLandWorldSettings& Settings, const FVoxelLandSerializedProgram& Payload,
		FVoxelLandZoneProfile& Profile)
	{
		Profile.VoxelSizeCm = Settings.VoxelSizeCm;
		for (const FVoxelLandParameterOverride& Override : Settings.Overrides)
		{
			const FVoxelLandSerializedParameter* Parameter =
				Payload.Parameters.FindByPredicate([&Override](const FVoxelLandSerializedParameter& Candidate) {
					return Candidate.Id == Override.ParameterId;
				});
			// Unreachable declarations need no generator slot.
			if (!Parameter)
			{
				continue;
			}
			switch (Override.Type)
			{
				case EVoxelLandGraphValueType::Float:
					Profile.FloatOverrides.Add(Parameter->Name, Override.FloatValue);
					break;
				case EVoxelLandGraphValueType::Int32:
					Profile.IntOverrides.Add(Parameter->Name, Override.IntValue);
					break;
				case EVoxelLandGraphValueType::Bool:
					Profile.BoolOverrides.Add(Parameter->Name, Override.BoolValue);
					break;
			}
		}
	}

	static bool OverridesEqual(
		const TArray<FVoxelLandParameterOverride>& A, const TArray<FVoxelLandParameterOverride>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].ParameterId != B[Index].ParameterId || A[Index].Type != B[Index].Type
				|| A[Index].FloatValue != B[Index].FloatValue || A[Index].IntValue != B[Index].IntValue
				|| A[Index].BoolValue != B[Index].BoolValue)
			{
				return false;
			}
		}
		return true;
	}

	class FInitializationRequest
	{
	public:
		virtual ~FInitializationRequest() = default;
		virtual bool IsCurrent() const = 0;
		virtual TOptional<TWeakObjectPtr<UMaterialInterface>> ResolvePresentation(AVoxelLandWorld&) const { return {}; }
		virtual void ApplyDemand(AVoxelLandWorld&) const = 0;
		virtual TOptional<FVoxelLandChunkKey> GetStartup() const { return {}; }
#if WITH_EDITOR
		virtual bool References(const UObject* Object) const = 0;
		virtual bool ReferencesPackage(const UPackage* Package) const = 0;
#endif
		virtual void Run(const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError,
			TArray<uint8>& OutM6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
			,
			const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>& Barrier
#endif
		) const = 0;
	};

	class FSettingsInitializationRequest final : public FInitializationRequest
	{
	public:
		FSettingsInitializationRequest(UVoxelLandWorldSettings& Settings, FVoxelLandGraphSnapshot&& InSnapshot,
			const FVoxelLandSerializedProgram& InPayload, FVoxelLandZoneProfile&& InProfile)
			: WeakSettings(&Settings)
			, WeakGraph(Settings.Graph)
			, Snapshot(MoveTemp(InSnapshot))
			, Payload(InPayload)
			, Profile(MoveTemp(InProfile))
			, Overrides(Settings.Overrides)
			, Seed(uint64(Settings.Seed))
			, SourceIdentity(FVoxelLandProgramCodec::SourceFingerprint(Snapshot))
			, HorizontalRadius(Settings.HorizontalRadius)
			, VerticalRadius(Settings.VerticalRadius)
		{
		}

		void ApplyDemand(AVoxelLandWorld& World) const override
		{
			World.HorizontalRadius = HorizontalRadius;
			World.VerticalRadius = VerticalRadius;
		}

		TOptional<TWeakObjectPtr<UMaterialInterface>> ResolvePresentation(AVoxelLandWorld& World) const override
		{
#if !UE_SERVER
			if (!World.bDataOnly && World.GetNetMode() != NM_DedicatedServer)
			{
				TStrongObjectPtr<UVoxelLandWorldSettings> Settings(WeakSettings.Get());
				if (Settings.IsValid())
				{
					return TWeakObjectPtr<UMaterialInterface>(ResolveTerrainMaterial(*Settings));
				}
			}
#endif
			return {};
		}

		bool IsCurrent() const override
		{
			UVoxelLandWorldSettings* Current = WeakSettings.Get();
			FVoxelLandGraphSnapshot CurrentSnapshot;
			FString Error;
			return Current && Current->ValidateSettings(Error) && uint64(Current->Seed) == Seed
				&& Current->VoxelSizeCm == Profile.VoxelSizeCm && Current->HorizontalRadius == HorizontalRadius
				&& Current->VerticalRadius == VerticalRadius && OverridesEqual(Current->Overrides, Overrides)
				&& Current->Graph->CaptureSnapshot(CurrentSnapshot, Error)
				&& FVoxelLandProgramCodec::SourceFingerprint(CurrentSnapshot) == SourceIdentity
				&& Current->Graph->CompiledProgram.ProgramFingerprint == Payload.ProgramFingerprint
				&& Current->Graph->CompiledProgram.SourceFingerprint == Payload.SourceFingerprint;
		}

#if WITH_EDITOR
		bool References(const UObject* Object) const
		{
			return WeakSettings.Get() == Object || WeakGraph.Get() == Object;
		}

		bool ReferencesPackage(const UPackage* Package) const
		{
			const UObject* Settings = WeakSettings.Get();
			const UObject* Graph = WeakGraph.Get();
			return (Settings && Settings->GetOutermost() == Package) || (Graph && Graph->GetOutermost() == Package);
		}
#endif
		void Run(const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError,
			TArray<uint8>& OutM6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
			,
			const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>& Barrier
#endif
		) const
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (Cancel->Load() || !FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, OutError) || Cancel->Load())
			{
				return;
			}
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
			if (Barrier)
			{
				Barrier->WaitForCancellation(Cancel);
				if (Cancel->Load())
				{
					return;
				}
			}
#endif
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			Settings.WorldSeed = Seed;
			Settings.Profile = Profile;
			if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, OutGenerator))
			{
				OutError = Failure.Message;
			}
		}

	private:
		TWeakObjectPtr<UVoxelLandWorldSettings> WeakSettings;
		TWeakObjectPtr<UVoxelLandTerrainGraph> WeakGraph;
		FVoxelLandGraphSnapshot Snapshot;
		FVoxelLandSerializedProgram Payload;
		FVoxelLandZoneProfile Profile;
		TArray<FVoxelLandParameterOverride> Overrides;
		uint64 Seed;
		uint64 SourceIdentity;
		int32 HorizontalRadius;
		int32 VerticalRadius;
	};

	// The process-wide limit is deliberately conservative: each admitted M4 world
	// reserves the full 128 MiB prepass bound for as long as any grid reader lives.
	// No sharing or cache reuse is claimed. Reducing the cvar never evicts readers;
	// it only prevents new reservations. The state outlives module static teardown.
	static constexpr int32 DefaultPrepassBudgetMiB = 256;
	static constexpr int64 BytesPerMiB = 1024 * 1024;
	inline static TAutoConsoleVariable<int32> CVarVoxelLandM4BudgetMiB{TEXT("voxel.M4.PrepassBudgetMiB"),
		DefaultPrepassBudgetMiB, TEXT("Process-wide M4 prepass/grid reservation budget in MiB."), ECVF_Default};

	struct FM4BudgetState
	{
		FCriticalSection Mutex;
		int64 Reserved = 0;
		int32 Validating = 0;
		void* RuntimeHandle = nullptr;
		void* AlgoHandle = nullptr;
		TSharedPtr<FM4PrepassCoordinator, ESPMode::ThreadSafe> Coordinator;
		bool bCoordinatorStopped = false;
	};

	static FM4BudgetState& M4BudgetState()
	{
		static FM4BudgetState* State = new FM4BudgetState();
		return *State;
	}

	class FM4GridReservation final : public FVoxelLandM4GridLease
	{
	public:
		static constexpr int64 Bytes = 128ll * 1024 * 1024;

		~FM4GridReservation() override
		{
			FM4BudgetState& State = M4BudgetState();
			FScopeLock Lock(&State.Mutex);
			State.Reserved -= Bytes;
		}
	};

	// Snapshot inputs are independently bounded before capture. A small fixed
	// number of validation permits prevents unbounded metadata allocations while
	// allowing matching consumers to reach key-first producer admission. This is
	// an admission bound, not a claim of measured allocator/backend bytes.
	class FM4ValidationReservation final : public FVoxelLandM4GridLease
	{
	public:
		~FM4ValidationReservation() override
		{
			FM4BudgetState& State = M4BudgetState();
			FScopeLock Lock(&State.Mutex);
			--State.Validating;
		}
	};
#if !IS_MONOLITHIC
	static void* PinM4Module(const FName Name)
	{
		if (!FModuleManager::Get().IsModuleLoaded(Name))
		{
			return nullptr;
		}
		const FString Filename = FModuleManager::Get().GetModuleFilename(Name);
		if (Filename.IsEmpty())
		{
			return nullptr;
		}
#if PLATFORM_WINDOWS
		const FString Absolute = FPaths::ConvertRelativePathToFull(Filename);
		return reinterpret_cast<void*>(::LoadLibraryW(*Absolute));
#elif PLATFORM_LINUX
		return FPlatformProcess::GetDllHandle(*Filename);
#else
		return nullptr;
#endif
	}
#endif
	static bool EnsureM4ModulesPinnedLocked(FM4BudgetState& State)
	{
#if !IS_MONOLITHIC
		if (!State.RuntimeHandle || !State.AlgoHandle)
		{
			// Module manager access is restricted to the game thread. Worker-side
			// budget reservations may only use pins acquired before dispatch.
			if (!IsInGameThread())
			{
				return false;
			}
			void* Runtime = PinM4Module(TEXT("VoxelLand"));
			void* Algo = PinM4Module(TEXT("VoxelLandAlgo"));
			if (!Runtime || !Algo)
			{
				if (Runtime)
				{
					FPlatformProcess::FreeDllHandle(Runtime);
				}
				if (Algo)
				{
					FPlatformProcess::FreeDllHandle(Algo);
				}
				return false;
			}
			State.RuntimeHandle = Runtime;
			State.AlgoHandle = Algo;
		}
#endif
		return true;
	}

	static TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ReserveM4Budget()
	{
		FM4BudgetState& State = M4BudgetState();
		FScopeLock Lock(&State.Mutex);
		const int64 Limit =
			int64(FMath::Max(0, FVoxelLandWorldSettingsInitInternals::CVarVoxelLandM4BudgetMiB.GetValueOnAnyThread()))
			* BytesPerMiB;
		if (State.Reserved > Limit || FM4GridReservation::Bytes > Limit - State.Reserved)
		{
			return nullptr;
		}
		// Keep both module references until process exit: the last immutable grid
		// and its reservation can be released on any thread after world shutdown.
		if (!EnsureM4ModulesPinnedLocked(State))
		{
			return nullptr;
		}
		State.Reserved += FM4GridReservation::Bytes;
		return MakeShared<FM4GridReservation, ESPMode::ThreadSafe>();
	}

	template <typename T> static bool M4StructArraysEqual(const TArray<T>& Left, const TArray<T>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!T::StaticStruct()->CompareScriptStruct(&Left[Index], &Right[Index], 0))
			{
				return false;
			}
		}
		return true;
	}

	static bool M4SourcesEqual(const FVoxelLandGraphSnapshot& A, const FVoxelLandGraphSnapshot& B)
	{
		if (A.SourceVersion != B.SourceVersion || !M4StructArraysEqual(A.Parameters, B.Parameters)
			|| !M4StructArraysEqual(A.Nodes, B.Nodes) || !M4StructArraysEqual(A.Links, B.Links)
			|| A.Functions.Num() != B.Functions.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Functions.Num(); ++Index)
		{
			const FVoxelLandGraphFunctionSnapshot& Left = A.Functions[Index];
			const FVoxelLandGraphFunctionSnapshot& Right = B.Functions[Index];
			if (Left.AssetPath != Right.AssetPath || Left.SourceVersion != Right.SourceVersion
				|| Left.OutputType != Right.OutputType || Left.OutputNode != Right.OutputNode
				|| Left.OutputPin != Right.OutputPin || !M4StructArraysEqual(Left.Inputs, Right.Inputs)
				|| !M4StructArraysEqual(Left.Nodes, Right.Nodes) || !M4StructArraysEqual(Left.Links, Right.Links))
			{
				return false;
			}
		}
		return true;
	}

	class FM4InitializationRequest final : public FInitializationRequest
	{
	public:
		FM4InitializationRequest(UVoxelLandM4WorldConfig& Config, FVoxelLandM4WorldSnapshot&& InSnapshot,
			FIntVector InStartup, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InLease,
			TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InValidationLease,
			UVoxelLandM6WorldConfig* M6, TOptional<FVoxelLandM6WorldOverlay>&& InOverlay)
			: WeakConfig(&Config)
			, WeakM6(M6)
			, Continental(Config.Continental.Graph)
			, Mountain(Config.Mountain.Graph)
			, Peaks(Config.Peaks.Graph)
			, ValidationLease(MoveTemp(InValidationLease))
			, Snapshot(MoveTemp(InSnapshot))
			, Startup(InStartup)
			, Lease(MoveTemp(InLease))
			, Overlay(MoveTemp(InOverlay))
		{
			// Capture weak dependency handles only on the game thread. Worker jobs
			// use the UObject-free function snapshots, while reload handlers can
			// cancel a request before a function module or asset is reinstanced.
			const FVoxelLandM4ControlSnapshot* Channels[] = {
				&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
			for (const FVoxelLandM4ControlSnapshot* Channel : Channels)
			{
				for (const FVoxelLandGraphFunctionSnapshot& Function : Channel->Source.Functions)
				{
					if (UObject* Dependency = FSoftObjectPath(Function.AssetPath).ResolveObject())
					{
						FunctionDependencies.AddUnique(TWeakObjectPtr<UObject>(Dependency));
					}
				}
			}
		}

		FM4InitializationRequest(UVoxelLandFiniteWorld& Definition,
			TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> InSource,
			FVoxelLandM4WorldSnapshot&& InSnapshot, FIntVector InStartup,
			TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InValidationLease)
			: WeakDefinition(&Definition)
			, Continental(Definition.Common.Continental.Graph)
			, Mountain(Definition.Common.Mountain.Graph)
			, Peaks(Definition.Common.Peaks.Graph)
			, ValidationLease(MoveTemp(InValidationLease))
			, Snapshot(MoveTemp(InSnapshot))
			, Startup(InStartup)
			, DefinitionSource(MoveTemp(InSource))
		{
			const FVoxelLandM4ControlSnapshot* Channels[] = {
				&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
			for (const FVoxelLandM4ControlSnapshot* Channel : Channels)
			{
				for (const FVoxelLandGraphFunctionSnapshot& Function : Channel->Source.Functions)
				{
					if (UObject* Dependency = FSoftObjectPath(Function.AssetPath).ResolveObject())
					{
						FunctionDependencies.AddUnique(TWeakObjectPtr<UObject>(Dependency));
					}
				}
			}
		}

		FM4InitializationRequest(const FVoxelLandM4SavedWorld& Saved, FIntVector InStartup,
			TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InLease,
			TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InValidationLease)
			: ValidationLease(MoveTemp(InValidationLease))
			, Snapshot(Saved.Snapshot)
			, Startup(InStartup)
			, Lease(MoveTemp(InLease))
			, SavedIdentity(Saved.Identity)
		{
		}

		bool IsCurrent() const override
		{
			if (SavedIdentity.IsSet())
			{
				return true; // Owned snapshot cannot be edited or reinstanced.
			}
			if (DefinitionSource.IsValid())
			{
				TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(WeakDefinition.Get());
				if (!Definition.IsValid() || Definition->Common.Continental.Graph != Continental.Get()
					|| Definition->Common.Mountain.Graph != Mountain.Get()
					|| Definition->Common.Peaks.Graph != Peaks.Get())
				{
					return false;
				}
				FString Error;
				const TSharedPtr<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Current =
					Definition->CaptureSource(Error);
				const FVoxelLandFiniteWorldSnapshot* OriginalFinite = DefinitionSource->GetFinite();
				const FVoxelLandFiniteWorldSnapshot* CurrentFinite = Current.IsValid() ? Current->GetFinite() : nullptr;
				return OriginalFinite && CurrentFinite && Definition->Common.Continental.Graph == Continental.Get()
					&& Definition->Common.Mountain.Graph == Mountain.Get()
					&& Definition->Common.Peaks.Graph == Peaks.Get()
					&& FVoxelLandFiniteWorldSnapshot::StaticStruct()->CompareScriptStruct(
						OriginalFinite, CurrentFinite, 0);
			}
			TStrongObjectPtr<UVoxelLandM4WorldConfig> ConfigGuard(WeakConfig.Get());
			TStrongObjectPtr<UVoxelLandM6WorldConfig> M6Guard(WeakM6.Get());
			UVoxelLandM4WorldConfig* Config = ConfigGuard.Get();
			if (!Config || Config->Continental.Graph != Continental.Get() || Config->Mountain.Graph != Mountain.Get()
				|| Config->Peaks.Graph != Peaks.Get() || !Config->MatchesSnapshot(Snapshot)
				|| !MatchesOverlay(M6Guard.Get(), Config, Overlay))
			{
				return false;
			}
			FVoxelLandM4WorldSnapshot Current;
			FString Error;
			if (!Config->CaptureSnapshot(Current, Error))
			{
				return false;
			}
			const FVoxelLandM4ControlSnapshot* Old[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
			const FVoxelLandM4ControlSnapshot* New[] = {&Current.Continental, &Current.Mountain, &Current.Peaks};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (!M4SourcesEqual(Old[Index]->Source, New[Index]->Source)
					|| !FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(
						&Old[Index]->Payload, &New[Index]->Payload, 0))
				{
					return false;
				}
			}
			return MatchesOverlay(M6Guard.Get(), Config, Overlay);
		}

		static bool MatchesOverlay(const UVoxelLandM6WorldConfig* M6, const UVoxelLandM4WorldConfig* Config,
			const TOptional<FVoxelLandM6WorldOverlay>& Captured)
		{
			return !Captured.IsSet() || (IsValid(M6) && M6->Baseline == Config
				&& FVoxelLandM6WorldOverlay::StaticStruct()->CompareScriptStruct(&M6->Overlay, &Captured.GetValue(), 0));
		}

		void ApplyDemand(AVoxelLandWorld& World) const override
		{
			World.HorizontalRadius = Snapshot.HorizontalRadius;
			World.VerticalRadius = Snapshot.VerticalRadius;
		}

		TOptional<FVoxelLandChunkKey> GetStartup() const override
		{
			FVoxelLandChunkKey Key;
			Key.Coordinate = Startup;
			return Key;
		}

		TOptional<TWeakObjectPtr<UMaterialInterface>> ResolvePresentation(AVoxelLandWorld& World) const override
		{
#if !UE_SERVER
			if (!World.bDataOnly && World.GetNetMode() != NM_DedicatedServer)
			{
				TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(WeakDefinition.Get());
				if (Definition.IsValid())
				{
					return TWeakObjectPtr<UMaterialInterface>(ResolveDefinitionTerrainMaterial(*Definition));
				}
				TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(WeakConfig.Get());
				if (Config.IsValid())
				{
					return TWeakObjectPtr<UMaterialInterface>(ResolveM4TerrainMaterial(*Config));
				}
			}
#endif
			return {};
		}

#if WITH_EDITOR
		bool References(const UObject* Object) const override
		{
			if (Object == WeakConfig.Get() || Object == WeakM6.Get() || Object == WeakDefinition.Get()
				|| Object == Continental.Get() || Object == Mountain.Get() || Object == Peaks.Get())
			{
				return true;
			}
			for (const TWeakObjectPtr<UObject>& Dependency : FunctionDependencies)
			{
				if (Dependency.Get() == Object)
				{
					return true;
				}
			}
			return false;
		}

		bool ReferencesPackage(const UPackage* Package) const override
		{
			const UObject* Objects[] = {
				WeakConfig.Get(), WeakM6.Get(), WeakDefinition.Get(), Continental.Get(), Mountain.Get(), Peaks.Get()};
			for (const UObject* Object : Objects)
			{
				if (Object && Object->GetOutermost() == Package)
				{
					return true;
				}
			}
			for (const TWeakObjectPtr<UObject>& Dependency : FunctionDependencies)
			{
				const UObject* Object = Dependency.Get();
				if (Object && Object->GetOutermost() == Package)
				{
					return true;
				}
			}
			return false;
		}
#endif

		void Run(const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError,
			TArray<uint8>& OutM6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
			,
			const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>& Barrier
#endif
		) const override
		{
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
			if (Barrier)
			{
				Barrier->WaitForCancellation(Cancel);
				if (Cancel->Load())
				{
					return;
				}
			}
#endif
			auto IsCancelled = [&Cancel]() {
				return Cancel->Load();
			};
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
			if (SavedIdentity.IsSet()
				&& !VoxelLandM4WorldIdentity::Matches(SavedIdentity.GetValue(), Snapshot, OutError))
			{
				return;
			}
			const FVoxelLandM6WorldOverlay* ActiveOverlay = Overlay.IsSet() && Overlay->IsActive() ? &Overlay.GetValue() : nullptr;
			if (Cancel->Load()
				|| !VoxelLandM4Preparation::Prepare(Snapshot, Lease, ValidationLease, IsCancelled, Result, OutError, ActiveOverlay))
			{
				return;
			}
			// Validate the entire requested startup bubble before publishing the generator.
			for (int32 Z = -Snapshot.VerticalRadius; Z <= Snapshot.VerticalRadius; ++Z)
			{
				for (int32 Y = -Snapshot.HorizontalRadius; Y <= Snapshot.HorizontalRadius; ++Y)
				{
					for (int32 X = -Snapshot.HorizontalRadius; X <= Snapshot.HorizontalRadius; ++X)
					{
						if (Cancel->Load())
						{
							return;
						}
						const int64 CX = int64(Startup.X) + X, CY = int64(Startup.Y) + Y, CZ = int64(Startup.Z) + Z;
						if (CX < MIN_int32 || CX > MAX_int32 || CY < MIN_int32 || CY > MAX_int32 || CZ < MIN_int32
							|| CZ > MAX_int32)
						{
							OutError = TEXT("M4 startup demand coordinate overflows");
							return;
						}
						FVoxelLandChunkKey Key;
						Key.Coordinate = FIntVector(int32(CX), int32(CY), int32(CZ));
						if (const FVoxelLandError Failure = Result->ValidateM4ChunkDemand(Key))
						{
							OutError = FString::Printf(TEXT("M4 startup demand is invalid: %s"), *Failure.Message);
							return;
						}
					}
				}
			}
			TArray<uint8> M6Identity;
			if (ActiveOverlay && !Cancel->Load() && !EncodeM6Identity(*Result, M6Identity, OutError))
			{
				return;
			}
			if (!Cancel->Load())
			{
				OutM6Identity = MoveTemp(M6Identity);
				OutGenerator = MoveTemp(Result);
			}
		}

	private:
		bool EncodeM6Identity(const FVoxelLandGenerator& Generator, TArray<uint8>& Identity, FString& Error) const
		{
			FVoxelLandM4IdentityRecord Baseline;
			if (!VoxelLandM4WorldIdentity::Capture(Snapshot, Baseline, Error))
			{
				return false;
			}
			FCanonicalWriter Writer;
			constexpr uint32 IdentityTag = 0x36574c56; // VLW6 in little endian.
			constexpr uint32 Version = 1;
			Writer.U32(IdentityTag);
			Writer.U32(Version);
			const TConstArrayView<uint8> Sections[] = {Baseline.CanonicalBytes, Generator.GetM6StampIdentity(),
				Generator.GetM6FilterIdentity(), Generator.GetM6RedistanceIdentity()};
			for (const TConstArrayView<uint8> Section : Sections)
			{
				Writer.U32(uint32(Section.Num()));
				if (!Section.IsEmpty())
				{
					Writer.Bytes.Append(Section.GetData(), Section.Num());
				}
			}
			Identity = MoveTemp(Writer.Bytes);
			return true;
		}

		TWeakObjectPtr<UVoxelLandM4WorldConfig> WeakConfig;
		TWeakObjectPtr<UVoxelLandM6WorldConfig> WeakM6;
		TWeakObjectPtr<UVoxelLandFiniteWorld> WeakDefinition;
		TWeakObjectPtr<UVoxelLandTerrainGraph> Continental, Mountain, Peaks;
		TArray<TWeakObjectPtr<UObject>> FunctionDependencies;
		// Declared before Snapshot so admission remains held until its memory is freed.
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease;
		FVoxelLandM4WorldSnapshot Snapshot;
		FIntVector Startup;
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease;
		TOptional<FVoxelLandM4IdentityRecord> SavedIdentity;
		TSharedPtr<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> DefinitionSource;
		TOptional<FVoxelLandM6WorldOverlay> Overlay;
	};

#if !UE_SERVER
	static UMaterialInterface* ResolveTerrainMaterial(const UVoxelLandWorldSettings& Settings)
	{
		const UVoxelLandMaterialSetup* Setup = Settings.MaterialSetup.LoadSynchronous();
		const TSoftObjectPtr<UMaterialInterface> MaterialPath =
			Setup && Setup->FormatVersion == 1 ? Setup->Material : TSoftObjectPtr<UMaterialInterface>();
		UMaterialInterface* Material = MaterialPath.LoadSynchronous();
		if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("VoxelLand: missing or unsupported terrain material; using the default surface material"));
			return UMaterial::GetDefaultMaterial(MD_Surface);
		}
		return Material;
	}

	static UMaterialInterface* ResolveDefinitionTerrainMaterial(const UVoxelLandFiniteWorld& Definition)
	{
		const UVoxelLandMaterialSetup* Setup = Definition.Common.MaterialSetup.LoadSynchronous();
		const TSoftObjectPtr<UMaterialInterface> MaterialPath =
			Setup && Setup->FormatVersion == 1 ? Setup->Material : TSoftObjectPtr<UMaterialInterface>();
		UMaterialInterface* Material = MaterialPath.LoadSynchronous();
		if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("VoxelLand: missing or unsupported terrain material; using the default surface material"));
			return UMaterial::GetDefaultMaterial(MD_Surface);
		}
		return Material;
	}

	static UMaterialInterface* ResolveM4TerrainMaterial(const UVoxelLandM4WorldConfig& Settings)
	{
		const UVoxelLandMaterialSetup* Setup = Settings.MaterialSetup.LoadSynchronous();
		// Copy the path before a second synchronous load can dispatch callbacks or GC.
		const TSoftObjectPtr<UMaterialInterface> MaterialPath =
			Setup && Setup->FormatVersion == 1 ? Setup->Material : TSoftObjectPtr<UMaterialInterface>();
		UMaterialInterface* Material = MaterialPath.LoadSynchronous();
		if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("VoxelLand: missing or unsupported terrain material; using the default surface material"));
			return UMaterial::GetDefaultMaterial(MD_Surface);
		}
		return Material;
	}
#endif
};

void VoxelLandM4Preparation::StartCoordinator()
{
	check(IsInGameThread());
	FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State = FVoxelLandWorldSettingsInitInternals::M4BudgetState();
	FScopeLock Lock(&State.Mutex);
	State.bCoordinatorStopped = false;
}

bool VoxelLandM4Preparation::EnsureCoordinatorReady()
{
	if (!IsInGameThread())
	{
		return false;
	}
	FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State = FVoxelLandWorldSettingsInitInternals::M4BudgetState();
	FScopeLock Lock(&State.Mutex);
	if (State.bCoordinatorStopped || !FVoxelLandWorldSettingsInitInternals::EnsureM4ModulesPinnedLocked(State))
	{
		return false;
	}
	if (!State.Coordinator)
	{
		State.Coordinator = MakeShared<FM4PrepassCoordinator, ESPMode::ThreadSafe>();
	}
	return true;
}

TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> VoxelLandM4Preparation::ReserveValidation()
{
	if (!IsInGameThread())
	{
		return nullptr;
	}
	FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State = FVoxelLandWorldSettingsInitInternals::M4BudgetState();
	FScopeLock Lock(&State.Mutex);
	if (State.bCoordinatorStopped || State.Validating >= 4
		|| !FVoxelLandWorldSettingsInitInternals::EnsureM4ModulesPinnedLocked(State))
	{
		return nullptr;
	}
	++State.Validating;
	return MakeShared<FVoxelLandWorldSettingsInitInternals::FM4ValidationReservation, ESPMode::ThreadSafe>();
}

void VoxelLandM4Preparation::ShutdownCoordinator()
{
	check(IsInGameThread());
	TSharedPtr<FM4PrepassCoordinator, ESPMode::ThreadSafe> Coordinator;
	{
		FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State =
			FVoxelLandWorldSettingsInitInternals::M4BudgetState();
		FScopeLock Lock(&State.Mutex);
		State.bCoordinatorStopped = true;
		Coordinator = MoveTemp(State.Coordinator);
	}
	// Never hold the budget lock while joining producer threads or releasing
	// cached grids: their leases acquire that same lock during destruction.
	if (Coordinator)
	{
		Coordinator->Shutdown();
	}
}

TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> VoxelLandM4Preparation::Reserve()
{
	if (!IsInGameThread())
	{
		return nullptr;
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease =
		FVoxelLandWorldSettingsInitInternals::ReserveM4Budget();
	if (Lease)
	{
		return Lease;
	}
	// Finite cache entries and completed task leases share the process budget
	// with unlimited worlds. Take a stable coordinator reference under the
	// budget lock, then release that lock before entering the coordinator:
	// its eviction releases leases that acquire the same budget lock.
	TSharedPtr<FM4PrepassCoordinator, ESPMode::ThreadSafe> Coordinator;
	{
		FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State =
			FVoxelLandWorldSettingsInitInternals::M4BudgetState();
		FScopeLock Lock(&State.Mutex);
		if (!State.bCoordinatorStopped)
		{
			Coordinator = State.Coordinator;
		}
	}
	if (Coordinator)
	{
		Coordinator->ReleaseIdleResults();
		Lease = FVoxelLandWorldSettingsInitInternals::ReserveM4Budget();
	}
	return Lease;
}

bool VoxelLandM4Preparation::Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError)
{
	return Prepare(Snapshot, Lease, nullptr, IsCancelled, OutGenerator, OutError);
}

bool VoxelLandM4Preparation::Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& OutError)
{
	return Prepare(Snapshot, Lease, ValidationLease, IsCancelled, OutGenerator, OutError, nullptr);
}

bool VoxelLandM4Preparation::Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& OutError, const FVoxelLandM6WorldOverlay* Overlay)
{
	if (!VoxelLandM4HeightStageValidation::Validate(Snapshot, OutError))
	{
		return false;
	}
	if (IsInGameThread()
	    || (!Lease.IsValid() && (Snapshot.Domain.Mode != EVoxelLandM4DomainMode::Finite || !ValidationLease.IsValid())))
	{
		OutError = TEXT("M4 preparation requires a worker thread and resource reservation");
		return false;
	}
	if (Snapshot.Domain.Mode != EVoxelLandM4DomainMode::Finite
	    && Snapshot.Domain.Mode != EVoxelLandM4DomainMode::Unlimited)
	{
		OutError = TEXT("Unsupported M4 domain mode");
		return false;
	}
	if (Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited
		&& ((Snapshot.bOverhangEnabled && Snapshot.OverhangAmplitudeQ8 != 0
				&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Snapshot.OverhangFrequencyPerCm))
			|| (Snapshot.HeightDetail.bEnabled && Snapshot.HeightDetail.AmplitudeQ8 != 0
				&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Snapshot.HeightDetail.FrequencyPerCm))))
	{
		OutError = TEXT(
			"M4 fixed lattice requires unlimited mode, no global erosion and representable Q24 shaping frequencies");
		return false;
	}
	if (Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited && Snapshot.Domain.bGlobalErosion)
	{
		OutError = TEXT("Unlimited M4 worlds cannot run global erosion");
		return false;
	}
	const FVoxelLandM4ControlSnapshot* ValidatedChannels[] = {
		&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	for (const FVoxelLandM4ControlSnapshot* Channel : ValidatedChannels)
	{
		if (!VoxelLandM4OverrideValidation::Validate(*Channel, OutError))
		{
			return false;
		}
		if (IsCancelled())
		{
			OutError = TEXT("M4 curve validation cancelled");
			return false;
		}
		if (!VoxelLandM4CurveValidation::Validate(*Channel, OutError))
		{
			return false;
		}
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generators[3];
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curves[3];
	FVoxelLandZoneProfile Profiles[3];
	// Stable channel namespaces keep reused graphs independent. Version 1.
	const FGuid ChannelIds[] = {
		FGuid(0x564c4d34, 0x434f4e54, 1, 1), FGuid(0x564c4d34, 0x4d4f554e, 1, 1), FGuid(0x564c4d34, 0x5045414b, 1, 1)};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (IsCancelled()
		    || !FVoxelLandProgramCodec::Load(
			    Channels[Index]->Source, Channels[Index]->Payload, Programs[Index], OutError)
		    || IsCancelled())
		{
			return false;
		}
		// Reject unsupported external workers, filters and 3D operations
		// before allocating any worker-local backend resources.
		if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Programs[Index]))
		{
			OutError = Failure.Message;
			return false;
		}
		VoxelLandM4OverrideValidation::BuildProfile(*Channels[Index], Snapshot.VoxelSizeCm, Profiles[Index]);
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Programs[Index];
		Settings.WorldSeed = VoxelLandDeterminism::DeriveNodeSeed(uint64(Snapshot.Seed), ChannelIds[Index], 0);
		Settings.Profile = Profiles[Index];
		if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generators[Index]))
		{
			OutError = Failure.Message;
			return false;
		}
		if (!FVoxelLandM4CurveLut::Create(
			Channels[Index]->CookedCurve.SamplesQ8, -512000000, 512000000, Curves[Index], OutError))
		{
			return false;
		}
	}
	FVoxelLandM4OverhangConfig Overhang;
	Overhang.Displacement.bEnabled = Snapshot.bOverhangEnabled;
	Overhang.Displacement.AmplitudeQ8 = Snapshot.OverhangAmplitudeQ8;
	Overhang.Displacement.BandHalfWidthQ8 = Snapshot.OverhangBandQ8;
	Overhang.FrequencyPerCm = Snapshot.OverhangFrequencyPerCm;
	Overhang.NodeId = Snapshot.OverhangNodeId;
	Overhang.Salt = uint32(Snapshot.OverhangSalt);
	FVoxelLandGeneratorSettings FinalSettings;
	FinalSettings.Program = Programs[0];
	FinalSettings.WorldSeed = uint64(Snapshot.Seed);
	FinalSettings.Profile = Profiles[0];
	if (Overlay)
	{
		Overlay->ApplyTo(FinalSettings);
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited)
	{
		FVoxelLandM4UnlimitedConfig Unlimited;
		Unlimited.ContinentalGenerator = Generators[0];
		Unlimited.MountainGenerator = Generators[1];
		Unlimited.PeaksGenerator = Generators[2];
		Unlimited.ContinentalCurve = Curves[0];
		Unlimited.MountainCurve = Curves[1];
		Unlimited.PeaksCurve = Curves[2];
		Unlimited.BaseQ8 = Snapshot.BaseHeightQ8;
		Unlimited.bNormalizeSlope = Snapshot.bNormalizeSlope;
		Unlimited.Overhang = Overhang;
		Unlimited.SmoothOverride = Snapshot.Smooth;
		Unlimited.HeightDetail = Snapshot.HeightDetail;
		if (const FVoxelLandError Failure = FVoxelLandGenerator::CreateM4Unlimited(FinalSettings, Unlimited, Result))
		{
			OutError = Failure.Message;
			return false;
		}
	}
	else
	{
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (Lease.IsValid())
		{
			// Compatibility path: a caller which already reserved a lease expects
			// that exact lease to be retained by its resulting grid.
			TUniquePtr<FVoxelLandWorkerContext> W0, W1, W2;
			if (const FVoxelLandError Failure = Generators[0]->CreateWorker(W0))
			{
				OutError = Failure.Message;
				return false;
			}
			if (const FVoxelLandError Failure = Generators[1]->CreateWorker(W1))
			{
				OutError = Failure.Message;
				return false;
			}
			if (const FVoxelLandError Failure = Generators[2]->CreateWorker(W2))
			{
				OutError = Failure.Message;
				return false;
			}
			uint64 Hash = 0;
			if (const FVoxelLandError Failure = VoxelLandM4ControlPipeline::Generate(
					{Snapshot.Domain, {*Generators[0], *W0, *Curves[0]}, {*Generators[1], *W1, *Curves[1]},
						{*Generators[2], *W2, *Curves[2]}, Snapshot.BaseHeightQ8, uint64(Snapshot.Seed),
						Snapshot.ErosionStageId, uint32(Snapshot.ErosionSalt), Snapshot.DropletCount, Snapshot.Erosion},
					Grid, Hash, IsCancelled, Lease))
			{
				OutError = Failure.Message;
				return false;
			}
		}
		else
		{
			TArray<uint8> Key;
			if (const FVoxelLandError Failure = VoxelLandM4PrepassIdentity::BuildCandidate(
				Snapshot, *Programs[0], *Programs[1], *Programs[2], Key, 1u))
			{
				OutError = Failure.Message;
				return false;
			}
			TSharedPtr<FM4PrepassCoordinator, ESPMode::ThreadSafe> Coordinator;
			{
				FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State =
					FVoxelLandWorldSettingsInitInternals::M4BudgetState();
				FScopeLock Lock(&State.Mutex);
				if (!State.bCoordinatorStopped)
				{
					Coordinator = State.Coordinator;
				}
			}
			if (!Coordinator)
			{
				OutError = TEXT("M4 shared prepass coordinator is unavailable");
				return false;
			}
			// Producer owns only immutable non-UObject state. Each producer creates
			// and destroys its three thread-affine workers on its own thread.
			const auto ProducerSnapshot = MakeShared<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe>(Snapshot);
			const auto G0 = Generators[0], G1 = Generators[1], G2 = Generators[2];
			const auto C0 = Curves[0], C1 = Curves[1], C2 = Curves[2];
			FM4PrepassCoordinator::FProducer Produce = [ProducerSnapshot, G0, G1, G2, C0, C1, C2](
				const FM4PrepassCoordinator::FLease& ProducerLease,
				TFunctionRef<bool()> ProducerCancelled,
				FM4PrepassCoordinator::FGrid& Grid, FString& Error) {
				TUniquePtr<FVoxelLandWorkerContext> W0, W1, W2;
				if (ProducerCancelled())
				{
					return false;
				}
				if (const FVoxelLandError Failure = G0->CreateWorker(W0))
				{
					Error = Failure.Message;
					return false;
				}
				if (const FVoxelLandError Failure = G1->CreateWorker(W1))
				{
					Error = Failure.Message;
					return false;
				}
				if (const FVoxelLandError Failure = G2->CreateWorker(W2))
				{
					Error = Failure.Message;
					return false;
				}
				uint64 Hash = 0;
				const FVoxelLandM4WorldSnapshot& S = *ProducerSnapshot;
				if (const FVoxelLandError Failure = VoxelLandM4ControlPipeline::Generate(
						{S.Domain, {*G0, *W0, *C0}, {*G1, *W1, *C1}, {*G2, *W2, *C2}, S.BaseHeightQ8, uint64(S.Seed),
							S.ErosionStageId, uint32(S.ErosionSalt), S.DropletCount, S.Erosion},
						Grid, Hash, ProducerCancelled, ProducerLease))
				{
					Error = Failure.Message;
					return false;
				}
				return !ProducerCancelled();
			};
			const TSharedPtr<FM4PrepassSubscription, ESPMode::ThreadSafe> Subscription = Coordinator->Subscribe(
				Key, [] { return FVoxelLandWorldSettingsInitInternals::ReserveM4Budget(); }, MoveTemp(Produce),
				OutError);
			if (!Subscription)
			{
				return false;
			}
			while (!Subscription->IsFinished())
			{
				if (IsCancelled())
				{
					Subscription->Cancel();
					return false;
				}
				FPlatformProcess::SleepNoStats(0.005f);
			}
			if (IsCancelled())
			{
				Subscription->Cancel();
				return false;
			}
			if (!Subscription->ReadResult(Grid, OutError))
			{
				return false;
			}
		}
		if (IsCancelled())
		{
			return false;
		}
		if (const FVoxelLandError Failure = FVoxelLandGenerator::CreateM4HeightField(FinalSettings, Grid,
			Snapshot.bNormalizeSlope, Overhang, Snapshot.Smooth, Snapshot.HeightDetail, Result))
		{
			OutError = Failure.Message;
			return false;
		}
	}

	if (IsCancelled())
	{
		return false;
	}
	OutGenerator = MoveTemp(Result);
	return true;
}

class FVoxelLandInitializationJob
{
public:
	explicit FVoxelLandInitializationJob(
		const TSharedRef<FVoxelLandWorldSettingsInitInternals::FInitializationRequest, ESPMode::ThreadSafe>& InRequest)
		: Request(InRequest)
		, Cancel(MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false))
	{
	}

	TSharedRef<FVoxelLandWorldSettingsInitInternals::FInitializationRequest, ESPMode::ThreadSafe> Request;
	TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Cancel;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FString Error;
	TArray<uint8> M6Identity;
};

bool AVoxelLandWorld::InitializeFromSettings(UVoxelLandWorldSettings* Settings)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
	    !World || World->bIsTearingDown || !HasActorBegunPlay())
	{
		LastInitializationError = TEXT("World actor is not ready for explicit initialization");
		return false;
	}
	LastInitializationError.Reset();
	if (!Settings || !Settings->ValidateSettings(LastInitializationError))
	{
		if (LastInitializationError.IsEmpty())
		{
			LastInitializationError = TEXT("World settings are missing");
		}
		return false;
	}
	StopDemo();
	StopPendingInitialization();
	++ConfigurationEpoch;
	TStrongObjectPtr<UVoxelLandWorldSettings> SettingsGuard(Settings);
	TStrongObjectPtr<UVoxelLandTerrainGraph> GraphGuard(Settings->Graph.Get());
	const uint64 CaptureEpoch = ConfigurationEpoch;
	FVoxelLandGraphSnapshot Snapshot;
	FString CaptureError;
	const bool bCaptured = GraphGuard->CaptureSnapshot(Snapshot, CaptureError);
	if (ConfigurationEpoch != CaptureEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
		|| GetWorld()->bIsTearingDown)
	{
		return false;
	}
	if (!bCaptured || Settings->Graph != GraphGuard.Get())
	{
		LastInitializationError = bCaptured ? TEXT("World graph changed during capture.") : MoveTemp(CaptureError);
		return false;
	}
	const FVoxelLandSerializedProgram Payload = GraphGuard->CompiledProgram;
	FVoxelLandZoneProfile Profile;
	FVoxelLandWorldSettingsInitInternals::CopyOverrides(*Settings, Payload, Profile);
	const TSharedRef<FVoxelLandWorldSettingsInitInternals::FSettingsInitializationRequest, ESPMode::ThreadSafe>
		Request = MakeShared<FVoxelLandWorldSettingsInitInternals::FSettingsInitializationRequest, ESPMode::ThreadSafe>(
			*Settings, MoveTemp(Snapshot), Payload, MoveTemp(Profile));
	const TSharedRef<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job =
		MakeShared<FVoxelLandInitializationJob, ESPMode::ThreadSafe>(Request);
	PendingInitialization = Job;
#if WITH_EDITOR
	InitPackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this,
		&AVoxelLandWorld::HandleInitPackageReload);
	InitObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this,
		&AVoxelLandWorld::HandleInitObjectsReplaced);
#endif
	bInitializing = true;
	SetActorTickEnabled(true);
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(InitReloadBarrierForTests);
	InitializationTask = Async(EAsyncExecution::ThreadPool, [Job, Barrier]()
#else
		InitializationTask = Async(EAsyncExecution::ThreadPool, [Job]()
#endif
		{
			Job->Request->Run(Job->Cancel, Job->Generator, Job->Error, Job->M6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
				, Barrier
#endif
				);
		});
	return true;
}

bool AVoxelLandWorld::InitializeFromWorldDefinition(UVoxelLandWorldDefinition* Definition)
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
		|| World->bIsTearingDown || !HasActorBegunPlay())
	{
		LastInitializationError = TEXT("World actor is not ready for explicit initialization.");
		return false;
	}
	UVoxelLandFiniteWorld* Finite = Cast<UVoxelLandFiniteWorld>(Definition);
	if (!IsValid(Finite) || Finite->Common.Caves.bEnabled || Finite->Common.Caves.Pockets.bEnabled)
	{
		LastInitializationError =
			IsValid(Definition) ? Definition->GetPreparationBlocker() : TEXT("World definition is missing or invalid.");
		return false;
	}
	LastInitializationError.Reset();
	StopDemo();
	StopPendingInitialization();
	++ConfigurationEpoch;
	if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		LastInitializationError = TEXT("M4 shared prepass coordinator is unavailable");
		return false;
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease =
		VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		LastInitializationError = TEXT("M4 snapshot validation capacity is exhausted");
		return false;
	}
	TStrongObjectPtr<UVoxelLandFiniteWorld> DefinitionGuard(Finite);
	const uint64 CaptureEpoch = ConfigurationEpoch;
	FString CaptureError;
	const TSharedPtr<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Source =
		Finite->CaptureSource(CaptureError);
	if (ConfigurationEpoch != CaptureEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
		|| GetWorld()->bIsTearingDown)
	{
		return false;
	}
	const FVoxelLandFiniteWorldSnapshot* FiniteSource = Source.IsValid() ? Source->GetFinite() : nullptr;
	if (!FiniteSource || FiniteSource->Common.Caves.bEnabled || FiniteSource->Common.Caves.Pockets.bEnabled)
	{
		LastInitializationError = FiniteSource ? Finite->GetPreparationBlocker() : MoveTemp(CaptureError);
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!FVoxelLandFiniteSnapshotAdapter::TryConvert(*FiniteSource, Snapshot, LastInitializationError))
	{
		return false;
	}
	// Select a deterministic point near the grid center; the worker validates
	// every chunk in the complete startup bubble before publishing anything.
	const int64 ChunkWidth = int64(Snapshot.VoxelSizeCm) * FVoxelLandChunkData::Cells;
	const int64 CenterX = Snapshot.Domain.OriginXCm + Snapshot.Domain.GridSpacingCm * Snapshot.Domain.CellsX / 2;
	const int64 CenterY = Snapshot.Domain.OriginYCm + Snapshot.Domain.GridSpacingCm * Snapshot.Domain.CellsY / 2;
	const int64 StartupX = CenterX / ChunkWidth - (CenterX % ChunkWidth < 0 ? 1 : 0);
	const int64 StartupY = CenterY / ChunkWidth - (CenterY % ChunkWidth < 0 ? 1 : 0);
	if (StartupX < MIN_int32 || StartupX > MAX_int32 || StartupY < MIN_int32 || StartupY > MAX_int32)
	{
		LastInitializationError = TEXT("M4 startup demand coordinate overflows");
		return false;
	}
	const FIntVector Startup(int32(StartupX), int32(StartupY), 0);
	const TSharedRef<FVoxelLandWorldSettingsInitInternals::FM4InitializationRequest, ESPMode::ThreadSafe> Request =
		MakeShared<FVoxelLandWorldSettingsInitInternals::FM4InitializationRequest, ESPMode::ThreadSafe>(
			*Finite, Source.ToSharedRef(), MoveTemp(Snapshot), Startup, MoveTemp(ValidationLease));
	const TSharedRef<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job =
		MakeShared<FVoxelLandInitializationJob, ESPMode::ThreadSafe>(Request);
	PendingInitialization = Job;
#if WITH_EDITOR
	InitPackageReloadHandle =
		FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this, &AVoxelLandWorld::HandleInitPackageReload);
	InitObjectsReplacedHandle =
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &AVoxelLandWorld::HandleInitObjectsReplaced);
#endif
	bInitializing = true;
	SetActorTickEnabled(true);
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(InitReloadBarrierForTests);
	InitializationTask = Async(EAsyncExecution::ThreadPool, [Job, Barrier]()
#else
	InitializationTask = Async(EAsyncExecution::ThreadPool, [Job]()
#endif
		{
			Job->Request->Run(Job->Cancel, Job->Generator, Job->Error, Job->M6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
				,
				Barrier
#endif
			);
		});
	return true;
}

bool AVoxelLandWorld::InitializeM4FromSettings(UVoxelLandM4WorldConfig* Settings, FIntVector StartupChunk)
{
	return InitializeAuthoredM4(Settings, StartupChunk, nullptr);
}

bool AVoxelLandWorld::InitializeM6FromSettings(UVoxelLandM6WorldConfig* Settings, FIntVector StartupChunk)
{
	check(IsInGameThread());
	if (!IsValid(Settings))
	{
		LastInitializationError = TEXT("M6 world settings are missing");
		return false;
	}
	return InitializeAuthoredM4(Settings->Baseline.Get(), StartupChunk, Settings);
}

bool AVoxelLandWorld::InitializeAuthoredM4(
	UVoxelLandM4WorldConfig* Settings, FIntVector StartupChunk, UVoxelLandM6WorldConfig* M6)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
		|| World->bIsTearingDown || !HasActorBegunPlay())
	{
		LastInitializationError = TEXT("World actor is not ready for explicit initialization");
		return false;
	}
	LastInitializationError.Reset();
	if (!IsValid(Settings))
	{
		LastInitializationError = M6 ? TEXT("Select a baseline M4 configuration in the M6 world asset")
			: TEXT("M4 world settings are missing");
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> SettingsGuard(Settings);
	TStrongObjectPtr<UVoxelLandM6WorldConfig> M6Guard(M6);
	TOptional<FVoxelLandM6WorldOverlay> Captured;
	if (M6)
	{
		if (const FVoxelLandError Error = FVoxelLandM6OverlayValidation::Validate(M6->Overlay, float(Settings->VoxelSizeCm)))
		{
			LastInitializationError = Error.Message;
			return false;
		}
		Captured.Emplace(M6->Overlay);
	}
	StopDemo();
	StopPendingInitialization();
	++ConfigurationEpoch;
	// Finite requests must be able to join an existing prepass before a new
	// 128 MiB producer reservation is attempted. Input capture is independently
	// bounded by the asset's preflight limits. Unlimited mode has no shared
	// global prepass and retains its original admission path.
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease;
	if (Settings->bUnlimited)
	{
		Lease = VoxelLandM4Preparation::Reserve();
	}
	else if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		LastInitializationError = TEXT("M4 shared prepass coordinator is unavailable");
		return false;
	}
	if (Settings->bUnlimited && !Lease)
	{
		LastInitializationError = TEXT("M4 prepass process budget is exhausted");
		return false;
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease =
		VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		LastInitializationError = TEXT("M4 snapshot validation capacity is exhausted");
		return false;
	}
	const uint64 CaptureEpoch = ConfigurationEpoch;
	FVoxelLandM4WorldSnapshot Snapshot;
	FString CaptureError;
	const bool bCaptured =
		Settings->ValidateBakedCurves(CaptureError) && Settings->CaptureSnapshot(Snapshot, CaptureError);
	// Capturing function dependencies can synchronously load assets and reenter.
	if (ConfigurationEpoch != CaptureEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
	    || GetWorld()->bIsTearingDown)
	{
		return false;
	}
	if (!bCaptured)
	{
		LastInitializationError = MoveTemp(CaptureError);
		return false;
	}
	using FRequest = FVoxelLandWorldSettingsInitInternals::FM4InitializationRequest;
	if (!FRequest::MatchesOverlay(M6Guard.Get(), Settings, Captured))
	{
		LastInitializationError = TEXT("M6 world settings changed during capture");
		return false;
	}
	const TSharedRef<FRequest, ESPMode::ThreadSafe> Request = MakeShared<FRequest, ESPMode::ThreadSafe>(
		*Settings, MoveTemp(Snapshot), StartupChunk, MoveTemp(Lease), MoveTemp(ValidationLease), M6Guard.Get(), MoveTemp(Captured));
	const TSharedRef<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job =
		MakeShared<FVoxelLandInitializationJob, ESPMode::ThreadSafe>(Request);
	PendingInitialization = Job;
#if WITH_EDITOR
	InitPackageReloadHandle =
		FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this, &AVoxelLandWorld::HandleInitPackageReload);
	InitObjectsReplacedHandle =
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &AVoxelLandWorld::HandleInitObjectsReplaced);
#endif
	bInitializing = true;
	SetActorTickEnabled(true);
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(InitReloadBarrierForTests);
	InitializationTask = Async(EAsyncExecution::ThreadPool, [Job, Barrier]()
#else
		InitializationTask = Async(EAsyncExecution::ThreadPool, [Job]()
#endif
		{
			Job->Request->Run(Job->Cancel, Job->Generator, Job->Error, Job->M6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
				,
				Barrier
#endif
				);
		});
	return true;
}

bool AVoxelLandWorld::InitializeM4FromSavedWorld(const FVoxelLandM4SavedWorld& Saved, FIntVector StartupChunk)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
	    || World->bIsTearingDown || !HasActorBegunPlay())
	{
		LastInitializationError = TEXT("World actor is not ready for explicit initialization");
		return false;
	}
	LastInitializationError.Reset();
	// Bound all copied metadata before acquiring a job or duplicating the record.
	if (!VoxelLandM4SavedWorldPreflight::Validate(Saved, LastInitializationError))
	{
		return false;
	}
	StopDemo();
	StopPendingInitialization();
	++ConfigurationEpoch;
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease;
	if (Saved.Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited)
	{
		Lease = VoxelLandM4Preparation::Reserve();
		if (!Lease)
		{
			LastInitializationError = TEXT("M4 prepass process budget is exhausted");
			return false;
		}
	}
	else if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		LastInitializationError = TEXT("M4 shared prepass coordinator is unavailable");
		return false;
	}
	auto ValidationLease = VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		LastInitializationError = TEXT("M4 snapshot validation capacity is exhausted");
		return false;
	}
	const TSharedRef<FVoxelLandWorldSettingsInitInternals::FM4InitializationRequest, ESPMode::ThreadSafe> Request =
		MakeShared<FVoxelLandWorldSettingsInitInternals::FM4InitializationRequest, ESPMode::ThreadSafe>(
			Saved, StartupChunk, MoveTemp(Lease), MoveTemp(ValidationLease));
	const TSharedRef<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job =
		MakeShared<FVoxelLandInitializationJob, ESPMode::ThreadSafe>(Request);
	PendingInitialization = Job;
#if WITH_EDITOR
	InitPackageReloadHandle =
		FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this, &AVoxelLandWorld::HandleInitPackageReload);
	InitObjectsReplacedHandle =
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &AVoxelLandWorld::HandleInitObjectsReplaced);
#endif
	bInitializing = true;
	SetActorTickEnabled(true);
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(InitReloadBarrierForTests);
	InitializationTask = Async(EAsyncExecution::ThreadPool, [Job, Barrier]()
#else
		InitializationTask = Async(EAsyncExecution::ThreadPool, [Job]()
#endif
		{
			Job->Request->Run(Job->Cancel, Job->Generator, Job->Error, Job->M6Identity
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
				,
				Barrier
#endif
				);
		});
	return true;
}

void AVoxelLandWorld::StopPendingInitialization()
{
#if WITH_EDITOR
	UnbindInitReloadHandlers();
#endif
	if (PendingInitialization)
	{
		PendingInitialization->Cancel->Store(true);
	}
	if (InitializationTask.IsValid())
	{
		InitializationTask.Wait();
		InitializationTask = TFuture<void>();
	}
	PendingInitialization.Reset();
	bInitializing = false;
	if (!State)
	{
		SetActorTickEnabled(false);
	}
}

void AVoxelLandWorld::PollInitialization()
{
	if (!PendingInitialization || !InitializationTask.IsValid() || !InitializationTask.IsReady())
	{
		return;
	}
	InitializationTask.Wait();
	InitializationTask = TFuture<void>();
#if WITH_EDITOR
	UnbindInitReloadHandlers();
#endif
	TSharedPtr<FVoxelLandInitializationJob, ESPMode::ThreadSafe> Job = MoveTemp(PendingInitialization);
	bInitializing = false;
	// Source recapture can synchronously load function assets and dispatch callbacks.
	// A callback may replace or tear down this actor's initialization.
	const uint64 SourceEpoch = ConfigurationEpoch;
	const bool bSourceCurrent = Job->Request->IsCurrent();
	if (ConfigurationEpoch != SourceEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
	    || GetWorld()->bIsTearingDown)
	{
		return;
	}
	if (!bSourceCurrent)
	{
		LastInitializationError = TEXT("Initialization result was superseded by changed settings or graph");
		SetActorTickEnabled(State.IsValid());
		OnInitializationFinished.Broadcast(false);
		return;
	}
	if (!Job->Generator)
	{
		LastInitializationError = Job->Error.IsEmpty() ? TEXT("Generator initialization failed") : Job->Error;
		SetActorTickEnabled(State.IsValid());
		OnInitializationFinished.Broadcast(false);
		return;
	}
	const uint64 PresentationEpoch = ConfigurationEpoch;
	const TOptional<TWeakObjectPtr<UMaterialInterface>> Presentation = Job->Request->ResolvePresentation(*this);
#if WITH_DEV_AUTOMATION_TESTS
	if (PresentationHookForTests)
	{
		TFunction<void()> Hook = MoveTemp(PresentationHookForTests);
		Hook();
	}
#endif
	// A reentrant request owns the actor now. Do not overwrite its error,
	// disable its tick, or broadcast a completion for the superseded request.
	if (ConfigurationEpoch != PresentationEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
	    || GetWorld()->bIsTearingDown)
	{
		return;
	}
	const bool bStillCurrent = Job->Request->IsCurrent();
	if (ConfigurationEpoch != PresentationEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
	    || GetWorld()->bIsTearingDown)
	{
		return;
	}
	if (!bStillCurrent)
	{
		LastInitializationError = TEXT("Initialization result was superseded during presentation setup");
		SetActorTickEnabled(State.IsValid());
		OnInitializationFinished.Broadcast(false);
		return;
	}
	const TOptional<FVoxelLandChunkKey> Startup = Job->Request->GetStartup();
	const uint64 StartupEpoch = ConfigurationEpoch;
	if (!TryInitialize(Job->Generator, Startup.IsSet() ? &Startup.GetValue() : nullptr, MoveTemp(Job->M6Identity)))
	{
		if (ConfigurationEpoch != StartupEpoch || bLifecycleEnded || IsActorBeingDestroyed() || !GetWorld()
			|| GetWorld()->bIsTearingDown)
		{
			return;
		}
		OnInitializationFinished.Broadcast(false);
		return;
	}
	Job->Request->ApplyDemand(*this);
	if (Presentation.IsSet())
	{
		SetTerrainMaterial(Presentation->Get());
	}
	LastInitializationError.Reset();
	OnInitializationFinished.Broadcast(true);
}

void AVoxelLandWorld::CancelInitialization()
{
	check(IsInGameThread());
	if (!PendingInitialization)
	{
		return;
	}
	StopPendingInitialization();
	++ConfigurationEpoch;
	LastInitializationError = TEXT("Initialization cancelled");
	OnInitializationFinished.Broadcast(false);
}

#if WITH_EDITOR
void AVoxelLandWorld::UnbindInitReloadHandlers()
{
	FCoreUObjectDelegates::OnPackageReloaded.Remove(InitPackageReloadHandle);
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(InitObjectsReplacedHandle);
	InitPackageReloadHandle.Reset();
	InitObjectsReplacedHandle.Reset();
}

void AVoxelLandWorld::HandleInitPackageReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PrePackageLoad && Event && PendingInitialization &&
	    PendingInitialization->Request->ReferencesPackage(Event->GetOldPackage()))
	{
		CancelInitialization();
	}
}

void AVoxelLandWorld::HandleInitObjectsReplaced(const TMap<UObject*, UObject*>& Map)
{
	if (!PendingInitialization) { return; }
	for (const TPair<UObject*, UObject*>& Replacement : Map)
	{
		if (PendingInitialization->Request->References(Replacement.Key))
		{
			CancelInitialization();
			return;
		}
	}
}
#endif
