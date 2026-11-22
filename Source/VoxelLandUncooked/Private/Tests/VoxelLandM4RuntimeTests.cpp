#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"
#include "World/VoxelLandM4Preparation.h"
#include "World/VoxelLandM4SavedWorld.h"

namespace
{
constexpr EAutomationTestFlags M4RuntimeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4RuntimeMakeProgram(UVoxelLandTerrainGraph& Graph, FString& Error)
{
	FVoxelLandGraphNodeRecord Constant;
	Constant.Id = FGuid(1, 1, 1, 1);
	Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2);
	Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
	Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
	Constant.Pins[1].bOutput = true;
	FVoxelLandGraphNodeRecord Output;
	Output.Id = FGuid(1, 1, 1, 2);
	Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1);
	Output.Pins[0].Id = FGuid(2, 1, 1, 3);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Constant.Id;
	Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id;
	Link.TargetPin = Output.Pins[0].Id;
	Graph.Nodes = {Constant, Output};
	Graph.Links = {Link};
	FVoxelLandGraphSnapshot Snapshot;
	if (!Graph.CaptureSnapshot(Snapshot, Error))
	{
		return false;
	}
	FVoxelLandSerializedProgram Payload;
	const bool bPacked = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		return FVoxelLandGraphCompiler::Compile(Snapshot, Draft, Error) && !FVoxelLandProgram::Validate(Draft, Program)
			&& FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
	}).Get();
	if (bPacked)
	{
		Graph.CompiledProgram = MoveTemp(Payload);
	}
	return bPacked;
}

void M4RuntimeSetCurve(FVoxelLandM4ControlConfig& Channel)
{
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	Last.InputQ16 = 65535;
	Channel.CurveKeys = {First, Last};
}

class FM4RuntimeCommand final : public IAutomationLatentCommand
{
public:
	FM4RuntimeCommand(FAutomationTestBase* InTest, UWorld* InWorld, AVoxelLandWorld* InActor,
		UVoxelLandM4WorldConfig* InConfig, IConsoleVariable* InBudget, int32 InPreviousBudget)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Config(InConfig)
		, Budget(InBudget)
		, PreviousBudget(InPreviousBudget)
		, Start(FPlatformTime::Seconds())
	{
	}

	bool Update() override
	{
		if (!IsValid(World) || !IsValid(Actor) || !IsValid(Config) || FPlatformTime::Seconds() - Start > 30.0)
		{
			Test->AddError(TEXT("M4 runtime initialization timed out or lost its fixture"));
			Cleanup();
			return true;
		}
		Actor->Tick(0.016f);
		if (Actor->bInitializing)
		{
			return false;
		}
		if (Phase == -1)
		{
			Test->TestFalse(TEXT("zero producer budget publishes no world"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("asynchronous budget rejection is explained"),
				Actor->LastInitializationError.Contains(TEXT("budget")));
			RestoreBudget();
			if (!Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
			{
				Test->AddError(TEXT("M4 initialization did not start after budget restoration"));
				Cleanup();
				return true;
			}
			// A material-only edit must not invalidate density while work is pending.
			Config->MaterialSetup = TSoftObjectPtr<UVoxelLandMaterialSetup>(
				FSoftObjectPath(TEXT("/Game/VoxelLandTests/ChangedMaterial.ChangedMaterial")));
			Phase = 0;
			return false;
		}
		if (Phase == 0)
		{
			if (Actor->HasWorldStateForTests() && !Actor->HasDataChunk(FIntVector::ZeroValue))
			{
				return false;
			}
			Test->TestTrue(TEXT("finite data-only world initialized"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("startup chunk reaches data-ready"), Actor->HasDataChunk(FIntVector::ZeroValue));
			Test->TestTrue(TEXT("success reports no error"), Actor->LastInitializationError.IsEmpty());
			if (!Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
			{
				Test->AddError(TEXT("M4 stale-result fixture did not start"));
				Cleanup();
				return true;
			}
			++Config->Seed;
			Phase = 1;
			return false;
		}
		if (Phase == 1)
		{
			Test->TestFalse(TEXT("changed settings cannot publish stale world"), Actor->HasWorldStateForTests());
			Test->TestTrue(
				TEXT("stale rejection is reported"), Actor->LastInitializationError.Contains(TEXT("superseded")));
			if (!Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
			{
				Test->AddError(TEXT("M4 graph-change fixture did not start"));
				Cleanup();
				return true;
			}
			Config->Continental.Graph->Nodes[0].Pins[0].FloatDefault = 0.25f;
			Phase = 2;
			return false;
		}
		if (Phase == 2)
		{
			Test->TestFalse(TEXT("changed graph cannot publish stale world"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("graph change reports stale rejection"),
				Actor->LastInitializationError.Contains(TEXT("superseded")));
			Config->Continental.Graph->Nodes[0].Pins[0].FloatDefault = 0.f;
			if (!Actor->InitializeM4FromSettings(Config, FIntVector(100, 0, 0)))
			{
				Test->AddError(TEXT("M4 boundary fixture did not start"));
				Cleanup();
				return true;
			}
			Phase = 3;
			return false;
		}
		if (Phase == 3)
		{
			Test->TestFalse(TEXT("out-of-domain startup cannot publish world"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("boundary rejection identifies startup"),
				Actor->LastInitializationError.Contains(TEXT("startup")));
			if (Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
			{
				Actor->CancelInitialization();
				Test->TestFalse(TEXT("cancel detaches initialization"), Actor->bInitializing);
				Test->TestFalse(TEXT("cancel cannot publish world"), Actor->HasWorldStateForTests());
			}
			else
			{
				Test->AddError(TEXT("M4 cancellation fixture did not start"));
			}
			FVoxelLandM4WorldSnapshot Snapshot;
			FString Error;
			if (!Config->CaptureSnapshot(Snapshot, Error)
				|| !Async(
					EAsyncExecution::ThreadPool, [&] { return VoxelLandM4SavedWorld::Capture(Snapshot, Saved, Error); })
					.Get()
				|| !Actor->InitializeM4FromSavedWorld(Saved, FIntVector::ZeroValue))
			{
				Test->AddError(FString::Printf(
					TEXT("Could not start saved-world fixture: %s %s"), *Error, *Actor->LastInitializationError));
				Cleanup();
				return true;
			}
			++Saved.Snapshot.Seed; // The running job owns its captured copy.
			Phase = 4;
			return false;
		}
		if (Phase == 4)
		{
			if (Actor->HasWorldStateForTests() && !Actor->HasDataChunk(FIntVector::ZeroValue))
			{
				return false;
			}
			Test->TestTrue(
				TEXT("saved world initializes independently of caller mutations"), Actor->HasWorldStateForTests());
			Test->TestTrue(TEXT("saved startup chunk is available"), Actor->HasDataChunk(FIntVector::ZeroValue));
			if (!Actor->InitializeM4FromSavedWorld(Saved, FIntVector::ZeroValue))
			{
				Test->AddError(TEXT("Tampered saved-world fixture did not dispatch"));
				Cleanup();
				return true;
			}
			Phase = 5;
			return false;
		}
		Test->TestFalse(TEXT("tampered saved identity publishes no world"), Actor->HasWorldStateForTests());
		Test->TestTrue(
			TEXT("tampered identity reports a mismatch"), Actor->LastInitializationError.Contains(TEXT("identity")));
		Saved.Snapshot.HorizontalRadius = 0;
		Test->TestFalse(TEXT("invalid saved startup radius rejected before dispatch"),
			Actor->InitializeM4FromSavedWorld(Saved, FIntVector::ZeroValue));
		Test->TestFalse(TEXT("preflight failure leaves no job"), Actor->bInitializing);
		Cleanup();
		return true;
	}

private:
	void RestoreBudget()
	{
		if (Budget)
		{
			Budget->SetWithCurrentPriority(PreviousBudget);
			Budget = nullptr;
		}
	}
	void Cleanup()
	{
		RestoreBudget();
		if (IsValid(Actor))
		{
			Actor->Shutdown();
		}
		if (IsValid(World))
		{
			World->DestroyWorld(false);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}
		if (IsValid(Config))
		{
			Config->RemoveFromRoot();
		}
	}
	FVoxelLandM4SavedWorld Saved;
	FAutomationTestBase* Test;
	UWorld* World;
	AVoxelLandWorld* Actor;
	UVoxelLandM4WorldConfig* Config;
	IConsoleVariable* Budget;
	int32 PreviousBudget;
	double Start;
	int32 Phase = -1;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4RuntimeInitializationTest, "VoxelLand.M4.Runtime.Initialization", M4RuntimeFlags)
bool FVoxelLandM4RuntimeInitializationTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("Engine unavailable"));
		return false;
	}
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->AddToRoot();
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	FString Error;
	if (!M4RuntimeMakeProgram(*Graph, Error))
	{
		AddError(FString::Printf(TEXT("Could not create compiled graph: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	M4RuntimeSetCurve(Config->Continental);
	M4RuntimeSetCurve(Config->Mountain);
	M4RuntimeSetCurve(Config->Peaks);
	TArray<int32> BakedCurve;
	BakedCurve.Init(0, 4097);
	if (!Config->SetBakedCurves(BakedCurve, BakedCurve, BakedCurve, 1, Error))
	{
		AddError(FString::Printf(TEXT("Could not prepare baked curves: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	Config->CellsX = Config->CellsY = 8;
	Config->OriginXCm = Config->OriginYCm = -25600;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	// Use a fresh prepass identity so another test's retained cache cannot
	// satisfy the zero-budget miss without invoking producer admission.
	const FGuid UniqueSeed = FGuid::NewGuid();
	Config->Seed = int64((uint64(UniqueSeed.A) << 32 | UniqueSeed.B) & uint64(MAX_int64));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		Config->RemoveFromRoot();
		AddError(TEXT("Could not create M4 world"));
		return false;
	}
	World->AddToRoot();
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!Actor)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Config->RemoveFromRoot();
		AddError(TEXT("Could not spawn M4 world actor"));
		return false;
	}
	Actor->bDataOnly = true;
	Actor->DispatchBeginPlay();
	TestFalse(TEXT("BeginPlay leaves terrain inert"), Actor->HasWorldStateForTests());
	IConsoleVariable* Budget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 admission budget exists"), Budget))
	{
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Config->RemoveFromRoot();
		return false;
	}
	const int32 PreviousBudget = Budget->GetInt();
	Budget->SetWithCurrentPriority(0);
	if (!Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
	{
		Budget->SetWithCurrentPriority(PreviousBudget);
		AddError(FString::Printf(TEXT("Could not dispatch budget-miss request: %s"), *Actor->LastInitializationError));
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Config->RemoveFromRoot();
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FM4RuntimeCommand(this, World, Actor, Config, Budget, PreviousBudget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4RuntimeSharedPrepassTest, "VoxelLand.M4.Runtime.SharedPrepass", M4RuntimeFlags)
bool FVoxelLandM4RuntimeSharedPrepassTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	Config->AddToRoot();
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config);
	FString Error;
	if (!M4RuntimeMakeProgram(*Graph, Error))
	{
		AddError(FString::Printf(TEXT("Could not create compiled graph: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	Config->Continental.Graph = Config->Mountain.Graph = Config->Peaks.Graph = Graph;
	M4RuntimeSetCurve(Config->Continental);
	M4RuntimeSetCurve(Config->Mountain);
	M4RuntimeSetCurve(Config->Peaks);
	TArray<int32> Samples;
	Samples.Init(0, 4097);
	Config->CellsX = Config->CellsY = 8;
	Config->OriginXCm = Config->OriginYCm = -25600;
	Config->GridSpacingCm = 6400;
	Config->bGlobalErosion = false;
	const FGuid UniqueSeed = FGuid::NewGuid();
	Config->Seed = int64((uint64(UniqueSeed.A) << 32 | UniqueSeed.B) & uint64(MAX_int64));
	if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		AddError(FString::Printf(TEXT("Could not prepare shared prepass fixture: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	const auto ValidationLease = VoxelLandM4Preparation::ReserveValidation();
	if (!TestTrue(TEXT("shared prepass validation admitted"), ValidationLease.IsValid()))
	{
		Config->RemoveFromRoot();
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!Config->SetBakedCurves(Samples, Samples, Samples, 1, Error) || !Config->CaptureSnapshot(Snapshot, Error))
	{
		AddError(FString::Printf(TEXT("Could not capture shared prepass fixture: %s"), *Error));
		Config->RemoveFromRoot();
		return false;
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> First, Second;
	const auto Prepare = [&Error, &ValidationLease](const FVoxelLandM4WorldSnapshot& Input,
							 TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Output) {
		return Async(EAsyncExecution::ThreadPool, [&Input, &Output, &Error, &ValidationLease] {
			return VoxelLandM4Preparation::Prepare(
				Input, nullptr, ValidationLease, [] { return false; }, Output, Error);
		}).Get();
	};
	const bool FirstOk = Prepare(Snapshot, First);
	TestTrue(TEXT("first validated request succeeds"), FirstOk);
	// Final smoothing is downstream of the global grid and must not invalidate
	// a proven upstream match. Each consumer still gets its own generator.
	FVoxelLandM4WorldSnapshot Downstream = Snapshot;
	Downstream.Smooth.bEnabled = true;
	Downstream.Smooth.Strength = 0.5f;
	const bool SecondOk = Prepare(Downstream, Second);
	TestTrue(TEXT("second validated request succeeds"), SecondOk);
	if (FirstOk && SecondOk && First && Second)
	{
		TestTrue(TEXT("matching prepass shares immutable grid"),
			First->GetM4HeightGrid().IsValid() && First->GetM4HeightGrid() == Second->GetM4HeightGrid());
		TestTrue(TEXT("downstream generators remain distinct"), First != Second);
	}
	FVoxelLandM4WorldSnapshot Stale = Snapshot;
	++Stale.Peaks.Payload.SourceFingerprint;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Previous = First;
	TestFalse(TEXT("stale program is rejected before a cache hit"), Prepare(Stale, Previous));
	TestTrue(TEXT("failed request leaves previous generator unchanged"), Previous == First);
	Config->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4RuntimeValidationAdmissionTest, "VoxelLand.M4.Runtime.ValidationAdmission", M4RuntimeFlags)
bool FVoxelLandM4RuntimeValidationAdmissionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("coordinator initialized"), VoxelLandM4Preparation::EnsureCoordinatorReady()))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>> Permits;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Permits.Add(VoxelLandM4Preparation::ReserveValidation());
		if (!TestTrue(TEXT("bounded validation slot admitted"), Permits.Last().IsValid()))
		{
			return false;
		}
	}
	TestFalse(TEXT("fifth validation slot rejected"), VoxelLandM4Preparation::ReserveValidation().IsValid());
	Permits.Pop();
	TestTrue(TEXT("releasing a permit restores capacity"), VoxelLandM4Preparation::ReserveValidation().IsValid());
	return true;
}

#endif
