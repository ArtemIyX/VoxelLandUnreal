#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Terrain/Runtime/VoxelLandRuntimeTestInputs.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Collision/VoxelLandSdfCollision.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/Collision/VoxelLandWalkMotor.h"
#include "World/Collision/VoxelLandWalkSettings.h"
#include "World/Collision/VoxelLandWalkState.h"
#include "World/VoxelLandWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandFourConsumerCollisionCommand final : public IAutomationLatentCommand
{
public:
	static constexpr int32 VoxelSizeCm = 100;
	static constexpr int32 SurfaceHeightCm = 200;
	static constexpr int32 ConsumerCount = 4;
	static constexpr int32 ResidentLimit = 1 + ConsumerCount * 2;
	static constexpr int64 WorldSeed = 1234;

	FVoxelLandFourConsumerCollisionCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Config(&InConfig)
	{
		ResidencyMs.Reserve(ConsumerCount);
		SweepUs.Reserve(SteadySteps * ConsumerCount);
		FrameUs.Reserve(SteadySteps);
		for (int32 Index = 0; Index < ConsumerCount; ++Index)
		{
			FConsumer& Consumer = Consumers[Index];
			const int32 SeamChunk = -12 + Index * 8;
			Consumer.Id = FGuid(1, 0, 0, Index + 1);
			Consumer.NegativeKey.Coordinate = FIntVector(SeamChunk - 1, 0, 0);
			Consumer.PositiveKey.Coordinate = FIntVector(SeamChunk, 0, 0);
			Consumer.SeamCm = SeamChunk * FVoxelLandChunkData::Cells * VoxelSizeCm;
			Consumer.Walk.Center = FVector3d(Consumer.SeamCm - 220.0, 1600.0, SurfaceHeightCm + 115.0);
		}
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
		{
			Test.AddError(FString::Printf(TEXT("Four-footprint streaming timed out (%d resident owners): %s"),
				ResidencyMs.Num(), *Terrain.LastInitializationError));
			return Finish();
		}
		const double FrameStartedAt = FPlatformTime::Seconds();
		Terrain.Tick(TickSeconds);
		if (Terrain.bInitializing)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("Headless Height world initialized"), Terrain.HasWorldStateForTests()))
		{
			Test.AddError(Terrain.LastInitializationError);
			return Finish();
		}
		if (!bDemandIssued)
		{
			bDemandIssued = AdmitFootprints();
			return bDemandIssued ? false : Finish();
		}
		if (!PollResidency())
		{
			return false;
		}

		for (FConsumer& Consumer : Consumers)
		{
			if (!AdvanceConsumer(Consumer))
			{
				return Finish();
			}
		}
		FrameUs.Add(ToMicroseconds(FPlatformTime::Seconds() - FrameStartedAt));
		if (FrameUs.Num() < SteadySteps)
		{
			return false;
		}
		VerifyReleaseAndReport();
		return Finish();
	}

private:
	class FConsumer
	{
	public:
		FGuid Id;
		FVoxelLandChunkKey NegativeKey;
		FVoxelLandChunkKey PositiveKey;
		double SeamCm = 0.0;
		FVoxelLandWalkState Walk;
		bool bIsResident = false;
	};

	static constexpr float TickSeconds = 1.0f / 60.0f;
	static constexpr double TimeoutSeconds = 30.0;
	static constexpr int32 SteadySteps = 80;

	bool AdmitFootprints()
	{
		DemandStartedAt = FPlatformTime::Seconds();
		for (const FConsumer& Consumer : Consumers)
		{
			const FBox SweptBounds(
				FVector(Consumer.SeamCm - 450.0, 1400.0, 100.0), FVector(Consumer.SeamCm + 450.0, 1800.0, 680.0));
			if (!Test.TestTrue(TEXT("Admit a bounded two-chunk footprint"),
					Terrain.UpdateCollisionDemandForBounds(Consumer.Id, SweptBounds)))
			{
				return false;
			}
			FVoxelLandCollisionWorldView Pending;
			if (!Test.TestTrue(TEXT("Capture a pending lease"), Terrain.CaptureCollisionView(Consumer.Id, Pending)))
			{
				return false;
			}
			const FVoxelLandCapsuleSweepResult Hit =
				FVoxelLandSdfCollision::SweepCapsule(*Pending.Field, GroundProbe(Consumer));
			if (!Test.TestTrue(TEXT("Pending terrain blocks the initial ground sweep"),
					Hit.Status == EVoxelLandCapsuleSweepStatus::Unavailable))
			{
				return false;
			}
			++BlockedInitialSweeps;
		}

		FVoxelLandChunkKey SpareKey;
		SpareKey.Coordinate = FIntVector(18, 0, 0);
		const FVoxelLandChunkKey Additional[] = {SpareKey};
		const bool bAdmitted = Terrain.UpdateCollisionDemand(FGuid(2, 0, 0, 1), Additional);
		RejectedDemands += bAdmitted ? 0 : 1;
		if (!Test.TestFalse(TEXT("Startup and four leases exhaust the resident cap"), bAdmitted))
		{
			return false;
		}

		const FConsumer& First = Consumers[0];
		const FBox Oversized(
			FVector(First.SeamCm - 210000.0, 1400.0, 100.0), FVector(First.SeamCm + 210000.0, 1800.0, 680.0));
		const bool bReplaced = Terrain.UpdateCollisionDemandForBounds(First.Id, Oversized);
		RejectedDemands += bReplaced ? 0 : 1;
		return Test.TestFalse(TEXT("Overlong sweep bounds cannot replace a valid lease"), bReplaced);
	}

	bool PollResidency()
	{
		for (FConsumer& Consumer : Consumers)
		{
			if (Consumer.bIsResident)
			{
				continue;
			}
			const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Negative =
				Terrain.FindChunk(Consumer.NegativeKey);
			const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Positive =
				Terrain.FindChunk(Consumer.PositiveKey);
			if (!Negative || !Positive)
			{
				++WaitingOwnerPolls;
				continue;
			}
			Consumer.bIsResident = true;
			ResidencyMs.Add(1000.0 * (FPlatformTime::Seconds() - DemandStartedAt));
			LeasedDensityBytes += Negative->Density.GetAllocatedSize() + Positive->Density.GetAllocatedSize();
		}
		return ResidencyMs.Num() == ConsumerCount;
	}

	static FVoxelLandCapsuleSweepRequest GroundProbe(const FConsumer& Consumer)
	{
		FVoxelLandCapsuleSweepRequest Request;
		Request.Start = FVector3d(Consumer.SeamCm, 1600.0, SurfaceHeightCm + 260.0);
		Request.End = FVector3d(Consumer.SeamCm, 1600.0, SurfaceHeightCm + 30.0);
		return Request;
	}

	bool AdvanceConsumer(FConsumer& Consumer)
	{
		FVoxelLandCollisionWorldView View;
		if (!Terrain.CaptureCollisionView(Consumer.Id, View))
		{
			Test.AddError(TEXT("Ready consumer lost its collision lease"));
			return false;
		}
		const FVoxelLandCapsuleSweepRequest Request = GroundProbe(Consumer);
		const double Started = FPlatformTime::Seconds();
		const FVoxelLandCapsuleSweepResult Hit = FVoxelLandSdfCollision::SweepCapsule(*View.Field, Request);
		SweepUs.Add(ToMicroseconds(FPlatformTime::Seconds() - Started));
		MaxCells = FMath::Max(MaxCells, Hit.CellsVisited);
		MaxRegions = FMath::Max(MaxRegions, Hit.RegionsVisited);
		if (Hit.Status != EVoxelLandCapsuleSweepStatus::Contact || Hit.Normal.Z < 0.99
			|| !FVoxelLandWalkMotor::Step(*View.Field, FVoxelLandWalkSettings(), FVector2d(1.0, 0.0), Consumer.Walk))
		{
			Test.AddError(FString::Printf(TEXT("Ready footprint failed traversal at X %.1f, status %d"),
				Consumer.Walk.Center.X, static_cast<int32>(Hit.Status)));
			return false;
		}
		return true;
	}

	void VerifyReleaseAndReport()
	{
		for (const FConsumer& Consumer : Consumers)
		{
			Test.TestTrue(TEXT("Consumer crossed its own chunk seam grounded"),
				Consumer.Walk.bIsGrounded && Consumer.Walk.Center.X > Consumer.SeamCm + 200.0);
		}
		const FConsumer& First = Consumers[0];
		FVoxelLandCollisionWorldView Retained;
		if (Test.TestTrue(
				TEXT("Retain a completed snapshot across eviction"), Terrain.CaptureCollisionView(First.Id, Retained)))
		{
			Terrain.ReleaseCollisionDemand(First.Id);
			Terrain.Tick(TickSeconds);
			Test.TestFalse(TEXT("First footprint left the resident world"),
				Terrain.FindChunk(First.NegativeKey).IsValid() || Terrain.FindChunk(First.PositiveKey).IsValid());
			Test.TestTrue(TEXT("Retained immutable field still grounds the released player"),
				FVoxelLandSdfCollision::SweepCapsule(*Retained.Field, GroundProbe(First)).Status
					== EVoxelLandCapsuleSweepStatus::Contact);
		}
		for (int32 Index = 1; Index < ConsumerCount; ++Index)
		{
			const FConsumer& Consumer = Consumers[Index];
			Test.TestTrue(TEXT("Other players retain both chunks after independent release"),
				Terrain.FindChunk(Consumer.NegativeKey).IsValid() && Terrain.FindChunk(Consumer.PositiveKey).IsValid());
		}
		for (const FConsumer& Consumer : Consumers)
		{
			Terrain.ReleaseCollisionDemand(Consumer.Id);
		}
		Terrain.Tick(TickSeconds);
		for (const FConsumer& Consumer : Consumers)
		{
			Test.TestFalse(TEXT("No collision-owned chunk remains after release"),
				Terrain.FindChunk(Consumer.NegativeKey).IsValid() || Terrain.FindChunk(Consumer.PositiveKey).IsValid());
		}
		const TActorIterator<AVoxelLandRegion> Regions(&World);
		Test.TestFalse(TEXT("Data-only traversal spawns no render regions"), bool(Regions));
		ReportTimings(TEXT("Initial footprint residency"), TEXT("ms"), ResidencyMs);
		ReportTimings(TEXT("Capsule sweep"), TEXT("us"), SweepUs);
		ReportTimings(TEXT("Four-owner frame including terrain tick, capture, probe and walking"), TEXT("us"), FrameUs);
		AddCountersReport();
	}

	void ReportTimings(const TCHAR* Label, const TCHAR* Unit, TArray<double>& Samples)
	{
		Samples.Sort();
		const int32 P50 = FMath::CeilToInt(Samples.Num() * 0.5) - 1;
		const int32 P95 = FMath::CeilToInt(Samples.Num() * 0.95) - 1;
		Test.AddInfo(FString::Printf(TEXT("%s p50 %.3f, p95 %.3f, max %.3f %s (%d samples)"), Label, Samples[P50],
			Samples[P95], Samples.Last(), Unit, Samples.Num()));
	}

	void AddCountersReport()
	{
		Test.AddInfo(FString::Printf(
			TEXT("Headless four-player fixture on %s: seed %lld, voxel %d cm, owners %d, peak %d leased chunks, ")
				TEXT("%lld allocated leased density bytes excluding startup, %d rejected demands, %d initial ")
					TEXT("blocked sweeps, %d waiting owner polls, max %d cells and %d regions per probe"),
			*FPlatformMisc::GetCPUBrand(), static_cast<long long>(WorldSeed), VoxelSizeCm, ConsumerCount,
			ConsumerCount * 2, static_cast<long long>(LeasedDensityBytes), RejectedDemands, BlockedInitialSweeps,
			WaitingOwnerPolls, MaxCells, MaxRegions));
	}

	static double ToMicroseconds(double Seconds) { return Seconds * 1.0e6; }

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
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	FConsumer Consumers[ConsumerCount];
	TArray<double> ResidencyMs;
	TArray<double> SweepUs;
	TArray<double> FrameUs;
	const double StartedAt = FPlatformTime::Seconds();
	double DemandStartedAt = 0.0;
	int64 LeasedDensityBytes = 0;
	int32 WaitingOwnerPolls = 0;
	int32 RejectedDemands = 0;
	int32 BlockedInitialSweeps = 0;
	int32 MaxCells = 0;
	int32 MaxRegions = 0;
	bool bDemandIssued = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandFourConsumerCollisionTest, "VoxelLand.Collision.World.FourConsumers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandFourConsumerCollisionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}

	using FCommand = FVoxelLandFourConsumerCollisionCommand;
	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	UVoxelLandTerrainGraph* const Graph = NewObject<UVoxelLandTerrainGraph>(Config.Get());
	FString Error;
	if (!FVoxelLandM4RuntimeTestInputs::MakeProgram(*Graph, Error))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandM4RuntimeTestInputs::BindAuthoredHeight(*Config.Get(), *Graph);
	Config->TerrainSource = EVoxelLandTerrainSource::HeightTerrain;
	Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
	Config->VoxelSizeCm = FCommand::VoxelSizeCm;
	Config->BaseElevationCm = FCommand::SurfaceHeightCm;
	Config->Seed = FCommand::WorldSeed;

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* const Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (TestNotNull(TEXT("Terrain actor created"), Terrain))
	{
		Terrain->bDataOnly = true;
		Terrain->MaxResidentChunks = FCommand::ResidentLimit;
		Terrain->DispatchBeginPlay();
		if (TestTrue(TEXT("Start real configured Height terrain"),
				Terrain->InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
		{
			ADD_LATENT_AUTOMATION_COMMAND(FCommand(*this, *World, *Terrain, *Config.Get()));
			return true;
		}
		AddError(Terrain->LastInitializationError);
		Terrain->Shutdown();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return false;
}

#endif
