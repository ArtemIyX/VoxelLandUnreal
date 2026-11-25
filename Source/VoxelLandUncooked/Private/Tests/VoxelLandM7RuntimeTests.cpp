#if WITH_DEV_AUTOMATION_TESTS && !UE_SERVER
#include "VoxelLandM7CheckpointTestFixture.h"

#include "Async/Async.h"
#include "Data/Structs/VoxelLandTestBarrier.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandWorld.h"

namespace
{
using FFixture = FVoxelLandM7CheckpointTestFixture;

FFixture::FGeneratorPtr MakeGenerator()
{
	constexpr int32 Cells = 80;
	TArray<int32> Heights;
	Heights.Init(1100 * 256, (Cells + 1) * (Cells + 1));
	FVoxelLandGeneratorSettings Settings;
	FFixture::FGridPtr Grid;
	FFixture::FGeneratorPtr Generator;
	if (!FFixture::CreateSettings(Settings)
		|| !FVoxelLandM4HeightGrid::Create(-40000, -40000, Cells, Cells, 1000, Heights, Grid)
		|| !FFixture::CreateGenerator(Settings, Grid, false, false, Generator))
	{
		return {};
	}
	return Generator;
}

int32 CountVisibleRegions(UWorld& World)
{
	int32 Count = 0;
	for (TActorIterator<AVoxelLandRegion> It(&World); It; ++It)
	{
		Count += It->IsHidden() ? 0 : 1;
	}
	return Count;
}

bool IsOnlyPublishedLeaf(const AVoxelLandWorld& Actor, const FVoxelLandChunkKey& Key)
{
	TArray<FVoxelLandChunkKey> Keys;
	Actor.CopyM7PublishedKeysForTests(Keys);
	return Keys.Num() == 1 && Keys[0] == Key;
}

class FVisualPublicationCommand final : public IAutomationLatentCommand
{
public:
	FVisualPublicationCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InActor,
		FFixture::FGeneratorPtr InGenerator)
		: Test(InTest), World(InWorld), Actor(InActor), Generator(MoveTemp(InGenerator)) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 30.0)
		{
			Test.AddError(FString::Printf(TEXT("M7 publication timed out at stage %d: %s"), int32(Stage), *Actor.M7ClipmapError));
			return Finish();
		}
		Actor.Tick(0.016f);
		switch (Stage)
		{
		case EStage::Initial:
			return CheckInitial();
		case EStage::ExplicitData:
			return CheckExplicitData();
		case EStage::BlockedReplacement:
			return CheckBlockedReplacement();
		case EStage::NewestReplacement:
			return CheckNewestReplacement();
		case EStage::Unsupported:
			return CheckUnsupported();
		case EStage::Headless:
			return CheckHeadless();
		}
		return Finish();
	}

private:
	enum class EStage : uint8
	{
		Initial, ExplicitData, BlockedReplacement, NewestReplacement, Unsupported, Headless
	};

	bool CheckInitial()
	{
		if (Actor.GetM7PublishedTicketForTests() == 0)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("First visual publication is an admitted coarse leaf"),
				IsOnlyPublishedLeaf(Actor, {FIntVector::ZeroValue, 1}))
			|| !Test.TestEqual(TEXT("One visible region"), CountVisibleRegions(World), 1))
		{
			return Finish();
		}
		ActiveTicket = Actor.GetM7PublishedTicketForTests();
		Actor.bEnableM7Clipmap = false; // Runtime flag changes apply on the next initialization.
		Test.TestFalse(TEXT("Explicit API rejects coarse data requests"), Actor.RequestChunk({FIntVector::ZeroValue, 1}));
		if (!Test.TestTrue(TEXT("Explicit LOD0 data request admitted"), Actor.RequestDataChunk(FIntVector::ZeroValue)))
		{
			return Finish();
		}
		Stage = EStage::ExplicitData;
		return false;
	}

	bool CheckExplicitData()
	{
		if (!Actor.HasDataChunk(FIntVector::ZeroValue))
		{
			return false;
		}
		Test.TestEqual(TEXT("Explicit data request does not duplicate the visual region"), CountVisibleRegions(World), 1);
		Test.TestFalse(TEXT("Visual coarse leaf is separate from explicit data records"),
			Actor.FindChunk({FIntVector::ZeroValue, 1}).IsValid());
		Barrier = MakeShared<FVoxelLandTestBarrier, ESPMode::ThreadSafe>();
		Actor.SetNextTestBarrier(Barrier);
		Actor.SetTestViewLocation(FVector(14400, 1600, 1600));
		Stage = EStage::BlockedReplacement;
		return false;
	}

	bool CheckBlockedReplacement()
	{
		Test.TestTrue(TEXT("In-flight replacement retains visible coverage"),
			Actor.GetM7PublishedTicketForTests() == ActiveTicket && IsOnlyPublishedLeaf(Actor, {FIntVector::ZeroValue, 1}));
		if (!Barrier->Reached->Wait(0))
		{
			return false;
		}
		Actor.SetTestViewLocation(FVector(-11200, 1600, 1600));
		Actor.Tick(0.016f); // Cancel the blocked CPU revision before releasing delivery.
		Barrier->Release->Trigger();
		Stage = EStage::NewestReplacement;
		return false;
	}

	bool CheckNewestReplacement()
	{
		const uint64 Ticket = Actor.GetM7PublishedTicketForTests();
		if (Ticket == ActiveTicket)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("Only the newest complete revision publishes"),
				IsOnlyPublishedLeaf(Actor, {FIntVector(-2, 0, 0), 1}))
			|| !Test.TestEqual(TEXT("Replacing the revision leaves one visible region"), CountVisibleRegions(World), 1))
		{
			return Finish();
		}
		ActiveTicket = Ticket;
		Actor.SetTestViewLocation(FVector(1000000, 1600, 1600));
		Stage = EStage::Unsupported;
		return false;
	}

	bool CheckUnsupported()
	{
		if (Actor.M7ClipmapError.IsEmpty() || Actor.IsM7ReplacementPendingForTests())
		{
			return false;
		}
		Test.TestTrue(TEXT("Unsupported finite coverage preserves the old revision"),
			Actor.GetM7PublishedTicketForTests() == ActiveTicket && IsOnlyPublishedLeaf(Actor, {FIntVector(-2, 0, 0), 1}));
		Test.TestEqual(TEXT("Old region stays visible after rejected replacement"), CountVisibleRegions(World), 1);
		Actor.bEnableM7Clipmap = true;
		Actor.bDataOnly = true;
		Actor.Initialize(Generator);
		Actor.SetTestViewLocation(FVector(1600, 1600, 1600));
		if (!Test.TestTrue(TEXT("Headless data still accepts explicit LOD0"),
			Actor.HasWorldStateForTests() && Actor.RequestDataChunk(FIntVector::ZeroValue)))
		{
			return Finish();
		}
		Stage = EStage::Headless;
		return false;
	}

	bool CheckHeadless()
	{
		if (!Actor.HasDataChunk(FIntVector::ZeroValue))
		{
			return false;
		}
		Test.TestFalse(TEXT("Data-only mode never queues a visual revision"), Actor.IsM7ReplacementPendingForTests());
		Test.TestEqual(TEXT("Data-only mode has no visible render regions"), CountVisibleRegions(World), 0);
		Test.TestEqual(TEXT("Data-only mode publishes no visual leaves"), Actor.M7PublishedLeafCount, 0);
		return Finish();
	}

	bool Finish()
	{
		if (Barrier)
		{
			Barrier->Release->Trigger();
		}
		Actor.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Actor;
	FFixture::FGeneratorPtr Generator;
	TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> Barrier;
	const double Start = FPlatformTime::Seconds();
	uint64 ActiveTicket = 0;
	EStage Stage = EStage::Initial;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7VisualPublicationTest, "VoxelLand.M7.Runtime.VisualPublication",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7VisualPublicationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("engine available"), GEngine))
	{
		return false;
	}
	FFixture::FGeneratorPtr Generator = Async(EAsyncExecution::Thread, &MakeGenerator).Get();
	if (!TestTrue(TEXT("finite M7 generator created"), Generator.IsValid()))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("terrain actor created"), Actor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Actor->WorkerCount = 1;
	Actor->bEnableM7Clipmap = true;
	Actor->M7MaxLOD = 1;
	Actor->M7MaxLeaves = 8;
	Actor->M7HorizontalRadiusCm = 600.0;
	Actor->M7VerticalRadiusCm = 400.0;
	Actor->DispatchBeginPlay();
	Actor->Initialize(Generator);
	Actor->SetTestViewLocation(FVector(1600, 1600, 1600));
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FVisualPublicationCommand>(*this, *World, *Actor, MoveTemp(Generator)));
	return true;
}
#endif
