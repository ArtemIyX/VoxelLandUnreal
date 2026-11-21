#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

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
	FM4RuntimeCommand(
		FAutomationTestBase* InTest, UWorld* InWorld, AVoxelLandWorld* InActor, UVoxelLandM4WorldConfig* InConfig)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Config(InConfig)
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
		Test->TestFalse(TEXT("out-of-domain startup cannot publish world"), Actor->HasWorldStateForTests());
		Test->TestTrue(
			TEXT("boundary rejection identifies startup"), Actor->LastInitializationError.Contains(TEXT("startup")));
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
		Cleanup();
		return true;
	}

private:
	void Cleanup()
	{
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
	FAutomationTestBase* Test;
	UWorld* World;
	AVoxelLandWorld* Actor;
	UVoxelLandM4WorldConfig* Config;
	double Start;
	int32 Phase = 0;
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
	const bool bAdmittedWithoutBudget = Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue);
	const FString BudgetError = Actor->LastInitializationError;
	Budget->SetWithCurrentPriority(PreviousBudget);
	TestFalse(TEXT("zero process budget rejects initialization"), bAdmittedWithoutBudget);
	TestTrue(TEXT("budget rejection is explained"), BudgetError.Contains(TEXT("budget")));
	TestFalse(TEXT("budget rejection publishes no world"), Actor->HasWorldStateForTests());
	if (!Actor->InitializeM4FromSettings(Config, FIntVector::ZeroValue))
	{
		AddError(FString::Printf(TEXT("Could not start M4 initialization: %s"), *Actor->LastInitializationError));
		Actor->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		Config->RemoveFromRoot();
		return false;
	}
	// No completion is pumped until the latent command ticks. A presentation-only
	// edit in that interval must not invalidate the density initialization.
	Config->MaterialSetup = TSoftObjectPtr<UVoxelLandMaterialSetup>(
		FSoftObjectPath(TEXT("/Game/VoxelLandTests/ChangedMaterial.ChangedMaterial")));
	ADD_LATENT_AUTOMATION_COMMAND(FM4RuntimeCommand(this, World, Actor, Config));
	return true;
}

#endif
