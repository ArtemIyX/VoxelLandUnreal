#include "World/VoxelLandWorld.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"
#include "World/Terrain/Worlds/VoxelLandAuthoredHeightProgram.h"
#include "World/Terrain/Heightfield/VoxelLandTerrainPrepassCoordinator.h"
#include "World/Terrain/Persistence/VoxelLandTerrainPrepassIdentity.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include "World/Terrain/Curves/VoxelLandCurveValidation.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorldPreflight.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "World/Configuration/VoxelLandFiniteSnapshotAdapter.h"

#include "Async/Async.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Data/World/Streaming/VoxelLandReloadTestBarrier.h"
#endif
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/World/Presentation/VoxelLandTerrainPresentation.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/World/Configuration/VoxelLandFiniteWorldSnapshot.h"
#include "Data/World/Configuration/VoxelLandWorldSourceSnapshot.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Generation/Terrain/Heightfield/VoxelLandTerrainPrepass.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightStageValidation.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Worlds/VoxelLandWorldDefinition.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"
#include "Data/Terrain/Shaping/VoxelLandOverhangConfig.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Engine/World.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipeline.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipelineInputs.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/TerrainOverlay/Configuration/VoxelLandTerrainOverlayValidation.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#if WITH_EDITOR
#include "UObject/PackageReload.h"
#endif
#if !UE_SERVER
#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"
#include "Misc/App.h"
#include "Materials/MaterialInterface.h"
#endif

class FVoxelLandWorldSettingsInitInternals final
{
public:
#if !UE_SERVER

#endif
	static void CopyOverrides(TConstArrayView<FVoxelLandParameterOverride> Overrides,
		const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandProgram& Program, FVoxelLandZoneProfile& Profile)
	{
		for (const FVoxelLandParameterOverride& Override : Overrides)
		{
			const FVoxelLandGraphParameter* Parameter =
				Snapshot.Parameters.FindByPredicate([&Override](const FVoxelLandGraphParameter& Candidate) {
					return Candidate.Id == Override.ParameterId;
				});
			// Only parameters used by the selected program have generator slots.
			if (!Parameter
				|| !Program.GetData().Parameters.ContainsByPredicate(
					[Parameter](const FVoxelLandParameterSlot& Slot) { return Slot.Name == Parameter->Name; }))
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
		virtual TOptional<TSoftObjectPtr<UVoxelLandMaterialSetup>> GetPresentationSetup() const { return {}; }
		virtual UVoxelLandWorldConfig* GetMaterialWorldConfig() const { return nullptr; }
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
		FSettingsInitializationRequest(UVoxelLandWorldConfig& Settings, FVoxelLandGraphSnapshot&& InSnapshot,
			const FVoxelLandSerializedProgram& InPayload, FVoxelLandZoneProfile&& InProfile,
			const FIntVector* InStartup, bool bInCompileProjection)
			: WeakSettings(&Settings)
			, WeakGraph(Settings.Graph)
			, OutputId(Settings.TerrainOutputId)
			, bCompileProjection(bInCompileProjection)
			, Snapshot(MoveTemp(InSnapshot))
			, Payload(InPayload)
			, Profile(MoveTemp(InProfile))
			, TerrainSource(Settings.TerrainSource)
			, WorldExtent(Settings.WorldExtent)
			, DensityBounds(Settings.IsDirectDensity() && !Settings.IsInfinite()
								? TOptional<FVoxelLandDensityBounds>(Settings.DensityBounds)
								: TOptional<FVoxelLandDensityBounds>())
			, Startup(InStartup ? TOptional<FIntVector>(*InStartup) : TOptional<FIntVector>())
			, Overrides(Settings.Overrides)
			, Seed(uint64(Settings.Seed))
			, SourceIdentity(FVoxelLandProgramCodec::SourceFingerprint(Snapshot))
			, HorizontalRadius(Settings.HorizontalRadius)
			, VerticalRadius(Settings.VerticalRadius)
		{
		}

		TOptional<FVoxelLandChunkKey> GetStartup() const override
		{
			if (!Startup.IsSet())
			{
				return {};
			}
			FVoxelLandChunkKey Key;
			Key.Coordinate = Startup.GetValue();
			return Key;
		}

		void ApplyDemand(AVoxelLandWorld& World) const override
		{
			World.HorizontalRadius = HorizontalRadius;
			World.VerticalRadius = VerticalRadius;
		}

		TOptional<TSoftObjectPtr<UVoxelLandMaterialSetup>> GetPresentationSetup() const override
		{
			if (const UVoxelLandWorldConfig* Settings = WeakSettings.Get())
			{
				return Settings->MaterialSetup;
			}
			return {};
		}

		UVoxelLandWorldConfig* GetMaterialWorldConfig() const override { return WeakSettings.Get(); }

		bool IsCurrent() const override
		{
			UVoxelLandWorldConfig* Current = WeakSettings.Get();
			FVoxelLandGraphSnapshot CurrentSnapshot;
			FString Error;
			return Current && Current->ValidateDirectDensitySettings(Error) && uint64(Current->Seed) == Seed
				&& Current->TerrainSource == TerrainSource && Current->WorldExtent == WorldExtent
				&& (!DensityBounds.IsSet() || Current->DensityBounds == DensityBounds.GetValue())
				&& Current->DirectVoxelSizeCm == Profile.VoxelSizeCm && Current->HorizontalRadius == HorizontalRadius
				&& Current->VerticalRadius == VerticalRadius && OverridesEqual(Current->Overrides, Overrides)
				&& Current->Graph == WeakGraph.Get() && Current->TerrainOutputId == OutputId
				&& (OutputId.IsValid() ? Current->CaptureBoundOutput(CurrentSnapshot, Error)
									   : Current->Graph->CaptureSnapshot(CurrentSnapshot, Error))
				&& FVoxelLandProgramCodec::SourceFingerprint(CurrentSnapshot) == SourceIdentity
				&& (bCompileProjection
					|| (Current->Graph->CompiledProgram.ProgramFingerprint == Payload.ProgramFingerprint
						&& Current->Graph->CompiledProgram.SourceFingerprint == Payload.SourceFingerprint));
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
			if (bCompileProjection && !Cancel->Load())
			{
				FVoxelLandProgramDraft Draft;
				if (!FVoxelLandGraphCompiler::Compile(Snapshot, Draft, OutError))
				{
					return;
				}
				if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
				{
					OutError = Failure.Message;
					return;
				}
			}
			if (Cancel->Load()
				|| (!bCompileProjection && !FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, OutError)))
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
			CopyOverrides(Overrides, Snapshot, *Program, Settings.Profile);
			Settings.DirectDensityBounds = DensityBounds;
			if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, OutGenerator))
			{
				OutError = Failure.Message;
			}
		}

	private:
		TWeakObjectPtr<UVoxelLandWorldConfig> WeakSettings;
		TWeakObjectPtr<UVoxelLandTerrainGraph> WeakGraph;
		FGuid OutputId;
		bool bCompileProjection = false;
		FVoxelLandGraphSnapshot Snapshot;
		FVoxelLandSerializedProgram Payload;
		FVoxelLandZoneProfile Profile;
		EVoxelLandTerrainSource TerrainSource;
		EVoxelLandWorldExtent WorldExtent;
		TOptional<FVoxelLandDensityBounds> DensityBounds;
		TOptional<FIntVector> Startup;
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
		FM4InitializationRequest(UVoxelLandWorldConfig& Config, FVoxelLandM4WorldSnapshot&& InSnapshot,
			FIntVector InStartup, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InLease,
			TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InValidationLease,
			TOptional<FVoxelLandM6WorldOverlay>&& InOverlay)
			: WeakConfig(&Config)
			, AuthoredGraph(Config.Graph)
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

		FM4InitializationRequest(UVoxelLandFiniteWorldSource& Definition,
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
				TStrongObjectPtr<UVoxelLandFiniteWorldSource> Definition(WeakDefinition.Get());
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
			TStrongObjectPtr<UVoxelLandWorldConfig> ConfigGuard(WeakConfig.Get());
			UVoxelLandWorldConfig* Config = ConfigGuard.Get();
			if (Snapshot.AuthoredHeightOutputId.IsValid())
			{
				return Config && Config->Graph == AuthoredGraph.Get() && Config->MatchesSnapshot(Snapshot)
					&& MatchesOverlay(Config, Overlay);
			}
			if (!Config || Config->Continental.Graph != Continental.Get() || Config->Mountain.Graph != Mountain.Get()
				|| Config->Peaks.Graph != Peaks.Get() || !Config->MatchesSnapshot(Snapshot)
				|| !MatchesOverlay(Config, Overlay))
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
			return MatchesOverlay(Config, Overlay);
		}

		static bool MatchesOverlay(
			const UVoxelLandWorldConfig* Config, const TOptional<FVoxelLandM6WorldOverlay>& Captured)
		{
			return IsValid(Config)
				&& (Captured.IsSet() ? FVoxelLandM6WorldOverlay::StaticStruct()->CompareScriptStruct(
										   &Config->Overlay, &Captured.GetValue(), 0)
									 : !Config->Overlay.IsActive());
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

		TOptional<TSoftObjectPtr<UVoxelLandMaterialSetup>> GetPresentationSetup() const override
		{
			if (const UVoxelLandFiniteWorldSource* Definition = WeakDefinition.Get())
			{
				return Definition->Common.MaterialSetup;
			}
			if (const UVoxelLandWorldConfig* Config = WeakConfig.Get())
			{
				return Config->MaterialSetup;
			}
			return {};
		}

		UVoxelLandWorldConfig* GetMaterialWorldConfig() const override { return WeakConfig.Get(); }

#if WITH_EDITOR
		bool References(const UObject* Object) const override
		{
			if (Object == WeakConfig.Get() || Object == WeakDefinition.Get() || Object == AuthoredGraph.Get() || Object == Continental.Get()
				|| Object == Mountain.Get() || Object == Peaks.Get())
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
				WeakConfig.Get(), WeakDefinition.Get(), AuthoredGraph.Get(), Continental.Get(), Mountain.Get(), Peaks.Get()};
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
			const FVoxelLandM6WorldOverlay* ActiveOverlay =
				Overlay.IsSet() && Overlay->IsActive() ? &Overlay.GetValue() : nullptr;
			if (Cancel->Load()
				|| !VoxelLandM4Preparation::Prepare(
					Snapshot, Lease, ValidationLease, IsCancelled, Result, OutError, ActiveOverlay))
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
			Writer.U32(IdentityTag);
			Writer.U32(Baseline.FormatVersion == VoxelLandM4WorldIdentity::AuthoredHeightVersion ? 2 : 1);
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

		TWeakObjectPtr<UVoxelLandWorldConfig> WeakConfig;
		TWeakObjectPtr<UVoxelLandTerrainGraph> AuthoredGraph;
		TWeakObjectPtr<UVoxelLandFiniteWorldSource> WeakDefinition;
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

class FVoxelLandAuthoredHeightPreparation final
{
public:
	static bool BuildGrid(const FVoxelLandM4WorldSnapshot& S, const FVoxelLandGenerator& Generator,
		const FM4PrepassCoordinator::FLease& Lease, TFunctionRef<bool()> IsCancelled,
		FM4PrepassCoordinator::FGrid& Grid, FString& Error)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Generator.CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		constexpr int32 BatchSize = 256;
		const int32 Width = S.Domain.CellsX + 1;
		const int32 Count = Width * (S.Domain.CellsY + 1);
		TArray<int32> Heights;
		Heights.Reserve(Count);
		TArray<FVector3f, TInlineAllocator<BatchSize>> Positions;
		TArray<float> Samples;
		for (int32 Start = 0; Start < Count; Start += BatchSize)
		{
			if (IsCancelled())
			{
				return false;
			}
			Positions.Reset();
			for (int32 I = Start; I < FMath::Min(Count, Start + BatchSize); ++I)
			{
				Positions.Emplace(float(S.Domain.OriginXCm + int64(I % Width) * S.Domain.GridSpacingCm),
					float(S.Domain.OriginYCm + int64(I / Width) * S.Domain.GridSpacingCm), 0.f);
			}
			if (const FVoxelLandError Failure = Generator.EvaluateSamplesRaw(Positions, *Worker, Samples))
			{
				Error = Failure.Message;
				return false;
			}
			for (const float Sample : Samples)
			{
				const double Value = double(S.BaseHeightQ8) + double(Sample) * double(S.HeightScaleCm) * 256.0;
				if (!FMath::IsFinite(Value) || FMath::Abs(Value) > 512000000.0)
				{
					Error = TEXT("Authored height exceeds the supported elevation range.");
					return false;
				}
				Heights.Add(int32(Value >= 0 ? Value + 0.5 : Value - 0.5));
			}
		}
		uint64 Hash = 0;
		if (const FVoxelLandError Failure = VoxelLandM4Prepass::Generate(S.Domain, Heights, uint64(S.Seed),
				S.ErosionStageId, uint32(S.ErosionSalt), S.DropletCount, S.Erosion, Grid, Hash, IsCancelled, Lease))
		{
			Error = Failure.Message;
			return false;
		}
		return !IsCancelled();
	}

	static bool Prepare(const FVoxelLandM4WorldSnapshot& S, const FM4PrepassCoordinator::FLease& Lease,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Out,
		FString& Error, const FVoxelLandM6WorldOverlay* Overlay)
	{
		if (IsInGameThread() || IsCancelled())
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Source;
		if (!VoxelLandAuthoredHeightProgram::CreateSource(S, Settings, Source, Error) || IsCancelled())
		{
			return false;
		}
		if (Overlay)
		{
			Overlay->ApplyTo(Settings);
		}
		FVoxelLandM4OverhangConfig Overhang;
		Overhang.Displacement.bEnabled = S.bOverhangEnabled;
		Overhang.Displacement.AmplitudeQ8 = S.OverhangAmplitudeQ8;
		Overhang.Displacement.BandHalfWidthQ8 = S.OverhangBandQ8;
		Overhang.FrequencyPerCm = S.OverhangFrequencyPerCm;
		Overhang.NodeId = S.OverhangNodeId;
		Overhang.Salt = uint32(S.OverhangSalt);
		FVoxelLandError Failure;
		if (S.Domain.Mode == EVoxelLandM4DomainMode::Unlimited)
		{
			FVoxelLandM4UnlimitedConfig Local;
			Local.AuthoredHeightGenerator = Source;
			Local.HeightScaleCm = S.HeightScaleCm;
			Local.BaseQ8 = S.BaseHeightQ8;
			Local.bNormalizeSlope = S.bNormalizeSlope;
			Local.Overhang = Overhang;
			Local.SmoothOverride = S.Smooth;
			Local.HeightDetail = S.HeightDetail;
			Failure = FVoxelLandGenerator::CreateM4Unlimited(Settings, Local, Out);
		}
		else
		{
			FM4PrepassCoordinator::FGrid Grid;
			if (!GetGrid(S, Source, Lease, IsCancelled, Grid, Error))
			{
				return false;
			}
			Failure = FVoxelLandGenerator::CreateM4HeightField(
				Settings, Grid, S.bNormalizeSlope, Overhang, S.Smooth, S.HeightDetail, Out);
		}
		if (Failure)
		{
			Error = Failure.Message;
			return false;
		}
		return !IsCancelled();
	}

private:
	static bool GetGrid(const FVoxelLandM4WorldSnapshot& Snapshot,
		const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Source,
		const FM4PrepassCoordinator::FLease& Lease, TFunctionRef<bool()> IsCancelled,
		FM4PrepassCoordinator::FGrid& Grid, FString& Error)
	{
		if (Lease)
		{
			return BuildGrid(Snapshot, *Source, Lease, IsCancelled, Grid, Error);
		}
		TSharedPtr<FM4PrepassCoordinator, ESPMode::ThreadSafe> Coordinator;
		{
			FVoxelLandWorldSettingsInitInternals::FM4BudgetState& State = FVoxelLandWorldSettingsInitInternals::M4BudgetState();
			FScopeLock Lock(&State.Mutex);
			Coordinator = State.Coordinator;
		}
		if (!Coordinator)
		{
			Error = TEXT("Shared height prepass coordinator is unavailable.");
			return false;
		}
		const auto Inputs = MakeShared<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe>(Snapshot);
		TArray<uint8> Key = VoxelLandAuthoredHeightProgram::BuildPrepassKey(Snapshot, Source->GetProgram());
		const TSharedPtr<FM4PrepassSubscription, ESPMode::ThreadSafe> Work = Coordinator->Subscribe(
			Key, [] { return FVoxelLandWorldSettingsInitInternals::ReserveM4Budget(); },
			[Inputs, Source](const FM4PrepassCoordinator::FLease& ProducerLease, TFunctionRef<bool()> Cancel,
				FM4PrepassCoordinator::FGrid& Result,
				FString& Problem) { return BuildGrid(*Inputs, *Source, ProducerLease, Cancel, Result, Problem); },
			Error);
		if (!Work)
		{
			return false;
		}
		while (!Work->IsFinished() && !IsCancelled())
		{
			FPlatformProcess::SleepNoStats(0.005f);
		}
		if (IsCancelled())
		{
			Work->Cancel();
			return false;
		}
		return Work->ReadResult(Grid, Error);
	}
};

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
	if (Snapshot.AuthoredHeightOutputId.IsValid())
	{
		return FVoxelLandAuthoredHeightPreparation::Prepare(Snapshot, Lease, IsCancelled, OutGenerator, OutError, Overlay);
	}
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
			FM4PrepassCoordinator::FProducer Produce =
				[ProducerSnapshot, G0, G1, G2, C0, C1, C2](const FM4PrepassCoordinator::FLease& ProducerLease,
					TFunctionRef<bool()> ProducerCancelled, FM4PrepassCoordinator::FGrid& Grid, FString& Error) {
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
							{S.Domain, {*G0, *W0, *C0}, {*G1, *W1, *C1}, {*G2, *W2, *C2}, S.BaseHeightQ8,
								uint64(S.Seed), S.ErosionStageId, uint32(S.ErosionSalt), S.DropletCount, S.Erosion},
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

bool AVoxelLandWorld::InitializeFromConfig(UVoxelLandWorldConfig* Config, FIntVector StartupChunk)
{
	check(IsInGameThread());
	if (!IsValid(Config))
	{
		LastInitializationError = TEXT("Select a VoxelLand World Config.");
		return false;
	}
	if ((Config->TerrainSource != EVoxelLandTerrainSource::HeightTerrain && !Config->IsDirectDensity())
		|| (Config->WorldExtent != EVoxelLandWorldExtent::Finite && !Config->IsInfinite()))
	{
		LastInitializationError = TEXT("World Config has an invalid terrain source or world extent.");
		return false;
	}
	if (Config->IsDirectDensity())
	{
		if (bEnableM7Clipmap && !bDataOnly && GetNetMode() != NM_DedicatedServer)
		{
			LastInitializationError = TEXT("Clipmap LODs are unavailable for Direct Density terrain.");
			return false;
		}
		if (!Config->IsInfinite() && !Config->DensityBounds.Contains(StartupChunk))
		{
			LastInitializationError = TEXT("The startup chunk must be inside the finite direct-density bounds.");
			return false;
		}
		return InitializeDirectDensity(Config, StartupChunk);
	}
	return InitializeAuthoredM4(Config, StartupChunk);
}

bool AVoxelLandWorld::InitializeDirectDensity(UVoxelLandWorldConfig* Settings, FIntVector StartupChunk)
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
	if (!Settings || !Settings->ValidateDirectDensitySettings(LastInitializationError))
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
	TStrongObjectPtr<UVoxelLandWorldConfig> SettingsGuard(Settings);
	TStrongObjectPtr<UVoxelLandTerrainGraph> GraphGuard(Settings->Graph.Get());
	const uint64 CaptureEpoch = ConfigurationEpoch;
	FVoxelLandGraphSnapshot Snapshot;
	FString CaptureError;
	const bool bCaptured = Settings->TerrainOutputId.IsValid() ? Settings->CaptureBoundOutput(Snapshot, CaptureError)
															   : GraphGuard->CaptureSnapshot(Snapshot, CaptureError);
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
	const FVoxelLandGraphNodeRecord* BoundNode = GraphGuard->Nodes.FindByPredicate(
		[Settings](const FVoxelLandGraphNodeRecord& Node) { return Node.Id == Settings->TerrainOutputId; });
	const bool bLegacySink = BoundNode
						  && BoundNode->OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput)
						  && !FVoxelLandGraphOutputs::HasNamedOutputs(GraphGuard->Nodes);
	const bool bCompileProjection = Settings->TerrainOutputId.IsValid() && !bLegacySink;
	const FVoxelLandSerializedProgram Payload =
		bCompileProjection ? FVoxelLandSerializedProgram() : GraphGuard->CompiledProgram;
	FVoxelLandZoneProfile Profile;
	Profile.VoxelSizeCm = Settings->DirectVoxelSizeCm;
	const TSharedRef<FVoxelLandWorldSettingsInitInternals::FSettingsInitializationRequest, ESPMode::ThreadSafe>
		Request = MakeShared<FVoxelLandWorldSettingsInitInternals::FSettingsInitializationRequest, ESPMode::ThreadSafe>(
			*Settings, MoveTemp(Snapshot), Payload, MoveTemp(Profile), &StartupChunk, bCompileProjection);
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

#if WITH_DEV_AUTOMATION_TESTS
bool AVoxelLandWorld::InitializeFiniteSourceForTests(UVoxelLandWorldSource* Definition)
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	if (bLifecycleEnded || IsActorBeingDestroyed() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World
		|| World->bIsTearingDown || !HasActorBegunPlay())
	{
		LastInitializationError = TEXT("World actor is not ready for explicit initialization.");
		return false;
	}
	UVoxelLandFiniteWorldSource* Finite = Cast<UVoxelLandFiniteWorldSource>(Definition);
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
	TStrongObjectPtr<UVoxelLandFiniteWorldSource> DefinitionGuard(Finite);
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

#endif

bool AVoxelLandWorld::InitializeAuthoredM4(UVoxelLandWorldConfig* Settings, FIntVector StartupChunk)
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
		LastInitializationError = TEXT("World Config is missing");
		return false;
	}
	if (!Settings->TerrainOutputId.IsValid())
	{
		LastInitializationError = TEXT(
			"Height World Config requires a named Float output. Reauthor legacy three-channel terrain and regenerate the world.");
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> SettingsGuard(Settings);
	TOptional<FVoxelLandM6WorldOverlay> Captured;
	if (Settings->Overlay.IsActive())
	{
		if (const FVoxelLandError Error =
				FVoxelLandM6OverlayValidation::Validate(Settings->Overlay, float(Settings->VoxelSizeCm)))
		{
			LastInitializationError = Error.Message;
			return false;
		}
		Captured.Emplace(Settings->Overlay);
	}
	StopDemo();
	StopPendingInitialization();
	++ConfigurationEpoch;
	// Finite requests must be able to join an existing prepass before a new
	// 128 MiB producer reservation is attempted. Input capture is independently
	// bounded by the asset's preflight limits. Unlimited mode has no shared
	// global prepass and retains its original admission path.
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease;
	if (Settings->IsInfinite())
	{
		Lease = VoxelLandM4Preparation::Reserve();
	}
	else if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		LastInitializationError = TEXT("M4 shared prepass coordinator is unavailable");
		return false;
	}
	if (Settings->IsInfinite() && !Lease)
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
	const bool bCaptured = Settings->CaptureSnapshot(Snapshot, CaptureError);
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
	if (!FRequest::MatchesOverlay(Settings, Captured))
	{
		LastInitializationError = TEXT("M6 world settings changed during capture");
		return false;
	}
	const TSharedRef<FRequest, ESPMode::ThreadSafe> Request = MakeShared<FRequest, ESPMode::ThreadSafe>(
		*Settings, MoveTemp(Snapshot), StartupChunk, MoveTemp(Lease), MoveTemp(ValidationLease), MoveTemp(Captured));
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
	const TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> CapturedSignals = MaterialWorldSignals;
	TOptional<FVoxelLandTerrainPresentation> Presentation;
#if !UE_SERVER
	TOptional<TSoftObjectPtr<UVoxelLandMaterialSetup>> PresentationSetup;
	TStrongObjectPtr<UVoxelLandWorldConfig> MaterialConfig;
	if (!bDataOnly && GetNetMode() != NM_DedicatedServer && FApp::CanEverRender())
	{
		PresentationSetup = Job->Request->GetPresentationSetup();
		if (PresentationSetup.IsSet())
		{
			MaterialConfig.Reset(Job->Request->GetMaterialWorldConfig());
			Presentation.Emplace(
				MaterialConfig ? FVoxelLandTerrainPresentationResolver::Resolve(this, *MaterialConfig, CapturedSignals)
							   : FVoxelLandTerrainPresentationResolver::Resolve(this, PresentationSetup.GetValue()));
		}
	}
#endif
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
	bool bPresentationCurrent = CapturedSignals == MaterialWorldSignals;
#if !UE_SERVER
	if (PresentationSetup.IsSet())
	{
		bPresentationCurrent =
			bPresentationCurrent && PresentationSetup == Job->Request->GetPresentationSetup()
			&& Presentation->SourceIdentity
				   == (MaterialConfig
						   ? FVoxelLandTerrainPresentationResolver::CaptureIdentity(*MaterialConfig)
						   : FVoxelLandTerrainPresentationResolver::CaptureIdentity(PresentationSetup.GetValue()));
	}
#endif
	if (!bStillCurrent || !bPresentationCurrent)
	{
		LastInitializationError = TEXT("Initialization result was superseded during presentation setup");
		SetActorTickEnabled(State.IsValid());
		OnInitializationFinished.Broadcast(false);
		return;
	}
	if (Presentation.IsSet() && Presentation->bHasConfiguredPalette && !Presentation->Appearance && State
		&& State->Appearance)
	{
		LastInitializationError = Presentation->Diagnostic;
		SetActorTickEnabled(true);
		OnInitializationFinished.Broadcast(false);
		return;
	}
	const TOptional<FVoxelLandChunkKey> Startup = Job->Request->GetStartup();
	const uint64 StartupEpoch = ConfigurationEpoch;
	const TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance =
		Presentation.IsSet() ? Presentation->Appearance : nullptr;
	if (!TryInitialize(
			Job->Generator, Startup.IsSet() ? &Startup.GetValue() : nullptr, MoveTemp(Job->M6Identity), Appearance))
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
		SetTerrainMaterial(Presentation->Material.Get());
#if !UE_SERVER
		MaterialWorldConfig = MaterialConfig.Get();
		ActiveMaterialLayout = Presentation->LayoutIdentity;
		ActiveMaterialSourceIdentity = Presentation->SourceIdentity;
#endif
		if (!Presentation->Diagnostic.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelLand: %s"), *Presentation->Diagnostic);
		}
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
	if (Phase == EPackageReloadPhase::PrePackageLoad && Event && PendingInitialization
		&& PendingInitialization->Request->ReferencesPackage(Event->GetOldPackage()))
	{
		CancelInitialization();
	}
}

void AVoxelLandWorld::HandleInitObjectsReplaced(const TMap<UObject*, UObject*>& Map)
{
	if (!PendingInitialization)
	{
		return;
	}
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
