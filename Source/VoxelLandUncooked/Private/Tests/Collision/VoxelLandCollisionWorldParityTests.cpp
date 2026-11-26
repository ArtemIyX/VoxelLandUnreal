#if WITH_DEV_AUTOMATION_TESTS && !UE_SERVER

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Generation/Collision/VoxelLandSdfCollision.h"
#include "Misc/AutomationTest.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandCollisionWorldParityCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;

	FVoxelLandCollisionWorldParityCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain,
		FHeightFixture::FGeneratorPtr InGenerator)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Generator(MoveTemp(InGenerator))
	{
	}

	static FHeightFixture::FGeneratorPtr MakeGenerator()
	{
		constexpr int32 Cells = 80;
		TArray<int32> Heights;
		Heights.Init(1100 * 256, (Cells + 1) * (Cells + 1));
		FVoxelLandGeneratorSettings Settings;
		FHeightFixture::FGridPtr Grid;
		FHeightFixture::FGeneratorPtr Result;
		if (!FHeightFixture::CreateSettings(Settings)
			|| !FVoxelLandM4HeightGrid::Create(-40000, -40000, Cells, Cells, 1000, Heights, Grid)
			|| !FHeightFixture::CreateGenerator(Settings, Grid, false, false, Result))
		{
			return {};
		}
		return Result;
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
		{
			Test.AddError(FString::Printf(TEXT("World collision parity timed out at stage %d: %s"),
				static_cast<int32>(Stage), *Terrain.M7ClipmapError));
			return Finish();
		}

		Terrain.Tick(0.016f);
		if (!Terrain.HasWorldStateForTests())
		{
			Test.AddError(TEXT("World collision parity terrain failed initialization"));
			return Finish();
		}
		if (!bHasLease)
		{
			return RequestFootprint() ? false : Finish();
		}
		if (!Terrain.FindChunk(FVoxelLandChunkKey{}) || !Terrain.FindChunk(SecondChunk))
		{
			return false;
		}
		if (Stage == EStage::Headless)
		{
			FVoxelLandCollisionWorldView View;
			if (Capture(View))
			{
				ExpectSameContact(TEXT("Data-only sweep"), View);
				CheckFiniteHeightEdge(View);
			}
			const TActorIterator<AVoxelLandRegion> Region(&World);
			Test.TestFalse(TEXT("Data-only run creates no terrain regions"), bool(Region));
			Test.TestEqual(TEXT("Data-only run publishes no render leaves"), Terrain.M7PublishedLeafCount, 0);
			return Finish();
		}

		AVoxelLandRegion* Region = FindPublishedRegion();
		if (!Region || Terrain.GetM7PublishedTicketForTests() == 0)
		{
			return false;
		}
		if (Stage == EStage::Coarse)
		{
			return VerifyCoarseAndRebase(*Region) ? false : Finish();
		}

		CheckPublishedLeaf(0);
		FVoxelLandCollisionWorldView FineView;
		if (!Capture(FineView))
		{
			return Finish();
		}
		ExpectSameContact(TEXT("LOD 0 visible sweep after regeneration"), FineView);
		Test.TestFalse(TEXT("Regeneration rejects a result from the preceding terrain epoch"),
			Terrain.IsCollisionViewCurrent(RebasedView));
		Terrain.bDataOnly = true;
		Terrain.Initialize(Generator);
		Stage = EStage::Headless;
		bHasLease = false;
		return false;
	}

private:
	enum class EStage : uint8
	{
		Coarse,
		Fine,
		Headless
	};

	static constexpr double TimeoutSeconds = 25.0;
	const FVector LogicalStart = FVector(3090, 1600, 1500);
	const FVector LogicalEnd = FVector(3310, 1600, 1125);
	const FVoxelLandChunkKey SecondChunk{FIntVector(1, 0, 0), 0};

	bool RequestFootprint()
	{
		const FVector Origin(World.OriginLocation);
		FBox Bounds(ForceInit);
		Bounds += LogicalStart - Origin;
		Bounds += LogicalEnd - Origin;
		bHasLease = Test.TestTrue(TEXT("Collision demand spans both LOD 0 chunks across the seam"),
			Terrain.UpdateCollisionDemandForBounds(Consumer, Bounds.ExpandBy(100.0)));
		return bHasLease;
	}

	AVoxelLandRegion* FindPublishedRegion() const
	{
		for (TActorIterator<AVoxelLandRegion> It(&World); It; ++It)
		{
			if (It->GetOwner() == &Terrain && It->IsChunkEffectivelyVisible(FIntVector::ZeroValue))
			{
				return *It;
			}
		}
		return nullptr;
	}

	void CheckPublishedLeaf(uint8 ExpectedLod) const
	{
		TArray<FVoxelLandChunkKey> Leaves;
		Terrain.CopyM7PublishedKeysForTests(Leaves);
		Test.TestTrue(FString::Printf(TEXT("Actual visible clipmap leaf with LOD %d"), ExpectedLod),
			Leaves.Num() == 1 && Leaves[0].Coordinate == FIntVector::ZeroValue && Leaves[0].LOD == ExpectedLod);
	}

	void CheckFiniteHeightEdge(const FVoxelLandCollisionWorldView& View) const
	{
		// The +X grid ends at 40,000 cm. This chunk starts inside the grid
		// but its required completed footprint extends beyond the edge.
		const FVoxelLandChunkKey Edge{FIntVector(12, 0, 0), 0};
		Test.TestFalse(TEXT("Finite Height rejects a crossing footprint"),
			Terrain.UpdateCollisionDemand(Consumer, MakeArrayView(&Edge, 1)));
		FVoxelLandCapsuleSweepRequest Request;
		Request.Start = FVector3d(39500.0, 1600.0, 1500.0);
		Request.End = FVector3d(39500.0, 1600.0, 1100.0);
		const FVoxelLandCapsuleSweepResult Hit = FVoxelLandSdfCollision::SweepCapsule(*View.Field, Request);
		Test.TestEqual(
			TEXT("Finite Height grid edge remains unavailable"), Hit.Status, EVoxelLandCapsuleSweepStatus::Unavailable);
		Test.TestEqual(TEXT("Missing finite Height footprint keeps the start"), Hit.LastSafeTime, 0.0);
		FVoxelLandCollisionWorldView AfterRejection;
		if (Capture(AfterRejection))
		{
			ExpectSameContact(TEXT("Rejected edge demand preserves the existing lease"), AfterRejection);
		}
	}

	bool Capture(FVoxelLandCollisionWorldView& View) const
	{
		return Test.TestTrue(TEXT("Capture a usable current collision view"),
			Terrain.CaptureCollisionView(Consumer, View) && Terrain.IsCollisionViewCurrent(View));
	}

	FVoxelLandCapsuleSweepResult Sweep(const FVoxelLandCollisionWorldView& View) const
	{
		const FVector WorldOffset(World.OriginLocation);
		const FVector LocalStart = LogicalStart - WorldOffset;
		const FVector LocalEnd = LogicalEnd - WorldOffset;
		FVoxelLandCapsuleSweepRequest Request;
		Request.Start = LocalStart + FVector(View.OriginLocation);
		Request.End = LocalEnd + FVector(View.OriginLocation);
		Test.TestEqual(TEXT("The captured origin recovers the same logical start"), Request.Start, LogicalStart);
		Test.TestEqual(TEXT("The captured origin recovers the same logical end"), Request.End, LogicalEnd);
		return FVoxelLandSdfCollision::SweepCapsule(*View.Field, Request);
	}

	void ExpectSameContact(const TCHAR* Description, const FVoxelLandCollisionWorldView& View) const
	{
		const FVoxelLandCapsuleSweepResult Result = Sweep(View);
		Test.TestTrue(Description, Result.Status == Baseline.Status && Result.Time == Baseline.Time
									   && Result.LastSafeTime == Baseline.LastSafeTime
									   && Result.Normal == Baseline.Normal && Result.Point == Baseline.Point
									   && Result.CellsVisited == Baseline.CellsVisited
									   && Result.RegionsVisited == Baseline.RegionsVisited);
	}

	bool VerifyCoarseAndRebase(AVoxelLandRegion& Region)
	{
		CheckPublishedLeaf(1);
		FVoxelLandCollisionWorldView OriginalView;
		if (!Capture(OriginalView))
		{
			return false;
		}
		Baseline = Sweep(OriginalView);
		if (!Test.TestEqual(TEXT("Downward sweep contacts the generated surface"), Baseline.Status,
				EVoxelLandCapsuleSweepStatus::Contact))
		{
			return false;
		}

		Terrain.SetRegionHidden(FIntVector::ZeroValue, true);
		Test.TestFalse(
			TEXT("The existing clipmap chunk is now hidden"), Region.IsChunkEffectivelyVisible(FIntVector::ZeroValue));
		FVoxelLandCollisionWorldView HiddenView;
		if (!Capture(HiddenView))
		{
			return false;
		}
		ExpectSameContact(TEXT("Hiding the published coarse chunk preserves the complete sweep"), HiddenView);

		if (!Test.TestTrue(
				TEXT("Unreal rebases the live world"), World.SetNewWorldOrigin(FIntVector(32000000, -32000000, 1000))))
		{
			return false;
		}
		Test.TestFalse(TEXT("The previous collision view cannot apply a result after rebasing"),
			Terrain.IsCollisionViewCurrent(OriginalView));
		if (!RequestFootprint() || !Capture(RebasedView))
		{
			return false;
		}
		Test.TestEqual(TEXT("Rebase leaves the terrain generation unchanged"), RebasedView.GetConfigurationEpoch(),
			OriginalView.GetConfigurationEpoch());
		ExpectSameContact(TEXT("The same logical sweep has identical results after rebasing"), RebasedView);

		Terrain.M7MaxLOD = 0;
		Terrain.Initialize(Generator);
		Terrain.SetTestViewLocation(FVector(1600.0, 1600.0, 1600.0) - FVector(World.OriginLocation));
		Stage = EStage::Fine;
		bHasLease = false;
		return true;
	}

	bool Finish()
	{
		Terrain.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	FHeightFixture::FGeneratorPtr Generator;
	FVoxelLandCollisionWorldView RebasedView;
	FVoxelLandCapsuleSweepResult Baseline;
	const FGuid Consumer = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	EStage Stage = EStage::Coarse;
	bool bHasLease = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCollisionWorldParityTest, "VoxelLand.Collision.World.RebaseAndRenderParity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCollisionWorldParityTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	FVoxelLandCollisionWorldParityCommand::FHeightFixture::FGeneratorPtr Generator =
		Async(EAsyncExecution::Thread, &FVoxelLandCollisionWorldParityCommand::MakeGenerator).Get();
	if (!TestTrue(TEXT("Create real Height generator"), Generator.IsValid()))
	{
		return false;
	}
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* const Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("Terrain actor created"), Terrain))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Terrain->WorkerCount = 1;
	Terrain->bEnableM7Clipmap = true;
	Terrain->M7MaxLOD = 1;
	Terrain->M7MaxLeaves = 8;
	Terrain->M7HorizontalRadiusCm = 600.0;
	Terrain->M7VerticalRadiusCm = 400.0;
	Terrain->DispatchBeginPlay();
	Terrain->Initialize(Generator);
	Terrain->SetTestViewLocation(FVector(1600.0, 1600.0, 1600.0));
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandCollisionWorldParityCommand(*this, *World, *Terrain, MoveTemp(Generator)));
	return true;
}

#endif
