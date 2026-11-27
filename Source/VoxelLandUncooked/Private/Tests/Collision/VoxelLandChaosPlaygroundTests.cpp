#if WITH_DEV_AUTOMATION_TESTS

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/Collision/Chaos/VoxelLandChaosTileInfo.h"
#include "World/VoxelLandWorld.h"

#include "Async/Async.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/WorldInitializationValues.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"

class FVoxelLandChaosPlaygroundCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;

	FVoxelLandChaosPlaygroundCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
	{
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandChaosSoakSeconds="), SoakSeconds);
		SoakSeconds = FMath::IsFinite(SoakSeconds) ? FMath::Clamp(SoakSeconds, 0.0, 3600.0) : 0.0;
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandChaosBaseline="), BaselineMode);
		FVoxelLandChunkKey SecondChunk;
		SecondChunk.Coordinate = FIntVector(1, 0, 0);
		Tiles.AddDefaulted();
		Tiles.Add(SecondChunk);
	}

	static FHeightFixture::FGeneratorPtr MakeGenerator()
	{
		constexpr int32 Cells = 80;
		constexpr int32 GridMinCm = -40000;
		constexpr int32 GridCellWidthCm = 1000;
		TArray<int32> Heights;
		Heights.Init(FloorHeightCm * 256, (Cells + 1) * (Cells + 1));
		if (UsesSlope())
		{
			for (int32 X = 0; X <= Cells; ++X)
			{
				const int32 HeightQ8 = FMath::RoundToInt32(HeightAt(GridMinCm + X * GridCellWidthCm, true) * 256.0);
				for (int32 Y = 0; Y <= Cells; ++Y)
				{
					Heights[X + Y * (Cells + 1)] = HeightQ8;
				}
			}
		}
		FVoxelLandGeneratorSettings Settings;
		FHeightFixture::FGridPtr Grid;
		FHeightFixture::FGeneratorPtr Generator;
		if (!FHeightFixture::CreateSettings(Settings)
			|| !FVoxelLandM4HeightGrid::Create(GridMinCm, GridMinCm, Cells, Cells, GridCellWidthCm, Heights, Grid)
			|| !FHeightFixture::CreateGenerator(Settings, Grid, false, false, Generator))
		{
			return {};
		}
		return Generator;
	}

	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > SoakSeconds + 40.0)
		{
			Test.AddError(FString::Printf(TEXT("Chaos playground stalled at phase %d, readiness %d"),
				static_cast<int32>(Phase), static_cast<int32>(Terrain.GetChaosCollisionReadiness(QueryLease))));
			for (int32 Index = 0; Index < Owners.Num(); ++Index)
			{
				Test.AddInfo(FString::Printf(TEXT("Chaos prop %d readiness %d at %s"), Index,
					static_cast<int32>(Terrain.GetChaosCollisionReadiness(Owners[Index])),
					*Props[Index]->GetComponentLocation().ToCompactString()));
			}
			return Finish();
		}
		World.Tick(LEVELTICK_All, StepSeconds);
		++PhysicsSteps;
		SampleStats();
		switch (Phase)
		{
			case EPhase::Start:
				return Start();
			case EPhase::Baseline:
				return UpdateBaseline(Now);
			case EPhase::Drop:
				return UpdateDrop(Now);
			case EPhase::Soak:
				return UpdateSoak(Now);
			case EPhase::Disable:
				return UpdateDisable();
			case EPhase::Restore:
				return UpdateRestore();
			case EPhase::AwaitCook:
				return UpdateAwaitCook();
			case EPhase::DrainDisabled:
				return UpdateDrainDisabled();
		}
		return Finish();
	}

private:
	enum class EPhase : uint8
	{
		Start,
		Baseline,
		Drop,
		Soak,
		Disable,
		Restore,
		AwaitCook,
		DrainDisabled
	};

	static constexpr int32 FloorHeightCm = 1100;
	static constexpr float StepSeconds = 1.0f / 60.0f;
	static constexpr double PropRadiusCm = 30.0;
	static constexpr double TraceX = 3200.0;
	static constexpr double TraceY = 2400.0;
	static constexpr double StartX = 2750.0;
	static constexpr double BoundaryX = 3200.0;

	static bool UsesSlope() { return FParse::Param(FCommandLine::Get(), TEXT("VoxelLandChaosSlopeCase")); }

	static double HeightAt(const double LogicalX, const bool bSloped)
	{
		constexpr double RisePerCm = 0.05;
		return FloorHeightCm + (bSloped ? LogicalX * RisePerCm : 0.0);
	}

	bool ConfigureSlopeTiles()
	{
		FVoxelLandChaosCollisionOverride Override;
		Override.Mode = EVoxelLandChaosCollisionOverrideMode::Enabled;
		Override.bOverrideDetail = true;
		Override.Detail = EVoxelLandChaosCollisionDetail::LOD1;
		const bool bSetLOD1 = Terrain.SetChaosCollisionOverride(MakeArrayView(&Tiles[0], 1), Override);
		Override.Detail = EVoxelLandChaosCollisionDetail::LOD2;
		const bool bSetLOD2 = Terrain.SetChaosCollisionOverride(MakeArrayView(&Tiles[1], 1), Override);
		return Test.TestTrue(
			TEXT("Apply collision LOD 1 and LOD 2 policy to adjacent slope tiles"), bSetLOD1 && bSetLOD2);
	}

	bool CheckSlopeTiles()
	{
		for (int32 Index = 0; Index < Tiles.Num(); ++Index)
		{
			FVoxelLandChaosTileInfo Info;
			if (!Test.TestTrue(TEXT("Get the active slope tile collision body"),
					Terrain.GetChaosCollisionTileInfo(Tiles[Index], Info) && Info.bHasShape && Info.bCoverageReady)
				|| !Test.TestEqual(
					TEXT("Adjacent slope tile installed its configured collision LOD"), Info.InstalledLod, Index + 1))
			{
				return false;
			}
		}
		return true;
	}

	bool Start()
	{
		const FVoxelLandChaosStats OffStats = Terrain.GetChaosCollisionStats();
		if (!Test.TestEqual(TEXT("Default-off world rejects Chaos leases"),
				Terrain.UpdateChaosCollisionTiles(QueryLease, Tiles), EVoxelLandChaosReadiness::Disabled)
			|| !Test.TestEqual(TEXT("Default-off world creates zero Chaos bodies"), OffStats.ActiveBodies, 0)
			|| !Test.TestEqual(TEXT("Default-off world starts zero cooks"), OffStats.CookActive, 0))
		{
			return Finish();
		}

		if (!BaselineMode.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
		{
			FVoxelLandChaosCollisionSettings Settings;
			Settings.bEnabled = true;
			Settings.RetirementGraceSeconds = 0.0;
			FString Diagnostic;
			if (!Terrain.ConfigureChaosTerrainCollision(Settings, Diagnostic))
			{
				Test.AddError(Diagnostic);
				return Finish();
			}
		}
		if (!BaselineMode.IsEmpty())
		{
			TRACE_BOOKMARK(TEXT("VoxelLand Chaos baseline start"));
			Phase = EPhase::Baseline;
			SoakStarted = FPlatformTime::Seconds();
			return false;
		}

		if (bSlope && !ConfigureSlopeTiles())
		{
			return Finish();
		}
		const EVoxelLandChaosReadiness Result = Terrain.UpdateChaosCollisionTiles(QueryLease, Tiles);
		if (!Test.TestEqual(TEXT("Uncooked seam starts pending"), Result, EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos playground demand"));
		constexpr int32 NumberOfDrops = 3;
		for (int32 Index = 0; Index < NumberOfDrops; ++Index)
		{
			if (!SpawnProp(Index))
			{
				return Finish();
			}
		}
		Phase = EPhase::Drop;
		return false;
	}

	bool UpdateBaseline(const double Now)
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (Stats.MeshActive != 0 || Stats.CookActive != 0 || Stats.ActiveBodies != 0)
		{
			Test.AddError(TEXT("A baseline without Chaos leases did terrain collision work."));
			return Finish();
		}
		return Now - SoakStarted >= FMath::Max(SoakSeconds, 1.0) ? Finish() : false;
	}

	bool SpawnProp(const int32 Index)
	{
		constexpr double StartDropHeightCm = 550.0;
		constexpr double InitialY = 1300.0;
		constexpr double SideSpacingCm = 175.0;
		const FVector Position(StartX, InitialY + Index * SideSpacingCm, HeightAt(StartX, bSlope) + StartDropHeightCm);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* const Prop = World.SpawnActor<AActor>(Position, FRotator::ZeroRotator, SpawnParameters);
		if (!Test.TestNotNull(TEXT("Create the dynamic sphere actor"), Prop))
		{
			return false;
		}

		USphereComponent* const Body = NewObject<USphereComponent>(Prop);
		Prop->SetRootComponent(Body);
		Prop->AddInstanceComponent(Body);
		Body->InitSphereRadius(PropRadiusCm);
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		Body->SetGenerateOverlapEvents(false);
		Body->SetWorldLocation(Position);
		Body->RegisterComponent();
		Body->SetSimulatePhysics(true);
		FGuid Owner;
		if (!Test.TestTrue(TEXT("Register an actual simulating sphere"), Terrain.RegisterChaosProp(*Body, Owner))
			|| !Test.TestFalse(TEXT("Managed prop waits for initial Chaos collision"), Body->IsSimulatingPhysics()))
		{
			return false;
		}
		Props.Add(Body);
		Owners.Add(Owner);
		TravelDirection.Add(1);
		LastSeamSide.Add(-1);
		return true;
	}

	bool HasSurfaceAtSeam() const
	{
		FHitResult Hit;
		const FVector Origin(World.OriginLocation);
		const double SeamHeight = HeightAt(TraceX, bSlope);
		const FVector Top = FVector(TraceX, TraceY, SeamHeight + 400.0) - Origin;
		const FVector Bottom = FVector(TraceX, TraceY, SeamHeight - 400.0) - Origin;
		return World.LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Pawn)
			&& FMath::IsNearlyEqual(Hit.ImpactPoint.Z + Origin.Z, SeamHeight, 5.0);
	}

	bool UpdateDrop(const double Now)
	{
		if (Terrain.GetChaosCollisionReadiness(QueryLease) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("A real Chaos Pawn trace hits the cooked seam"), HasSurfaceAtSeam())
			|| (bSlope && !CheckSlopeTiles()))
		{
			return Finish();
		}
		bool bAllResumed = true;
		for (const USphereComponent* Body : Props)
		{
			if (!CheckBody(*Body))
			{
				return Finish();
			}
			bAllResumed &= Body->IsSimulatingPhysics();
		}
		if (!bAllResumed)
		{
			return false;
		}
		if (bSlope)
		{
			Test.AddInfo(TEXT("Spheres resumed on a 5% authored slope with neighboring collision LOD 1 and LOD 2."));
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos playground drops resumed"));
		Phase = EPhase::Soak;
		SoakStarted = Now;
		NextRebuild = Now + 5.0;
		return false;
	}

	bool CheckBody(const USphereComponent& Body)
	{
		const FVector Position = Body.GetComponentLocation() + FVector(World.OriginLocation);
		const double GroundHeight = HeightAt(Position.X, bSlope);
		if (Position.Z < GroundHeight - PropRadiusCm)
		{
			++FallThroughs;
			Test.AddError(
				FString::Printf(TEXT("A managed sphere fell through Chaos terrain at x=%.1f: z=%.1f, terrain z=%.1f"),
					Position.X, Position.Z, GroundHeight));
			return false;
		}
		return true;
	}

	void DriveAcrossSeam(const int32 Index)
	{
		USphereComponent& Body = *Props[Index];
		const FVector Position = Body.GetComponentLocation();
		if (Position.X > BoundaryX + PropRadiusCm && LastSeamSide[Index] < 0)
		{
			LastSeamSide[Index] = 1;
			++SeamCrossings;
		}
		else if (Position.X < BoundaryX - PropRadiusCm && LastSeamSide[Index] > 0)
		{
			LastSeamSide[Index] = -1;
			++SeamCrossings;
		}

		const double SettledAltitude =
			HeightAt(Position.X + World.OriginLocation.X, bSlope) + PropRadiusCm + 8.0 - World.OriginLocation.Z;
		constexpr double RightTurnX = 3750.0;
		constexpr double LeftTurnX = 2650.0;
		if (!Body.IsSimulatingPhysics() || Position.Z >= SettledAltitude)
		{
			return;
		}
		if (SoakSeconds == 0.0 && Position.X >= RightTurnX)
		{
			Body.SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body.SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			Body.PutRigidBodyToSleep();
			return;
		}

		if (Position.X >= RightTurnX)
		{
			TravelDirection[Index] = -1;
		}
		else if (Position.X <= LeftTurnX)
		{
			TravelDirection[Index] = 1;
		}
		constexpr double CrossSeamSpeed = 600.0;
		const FVector Velocity = Body.GetPhysicsLinearVelocity();
		Body.SetPhysicsLinearVelocity(FVector(TravelDirection[Index] * CrossSeamSpeed, 0.0, Velocity.Z));
	}

	bool UpdateSoak(const double Now)
	{
		for (int32 Index = 0; Index < Props.Num(); ++Index)
		{
			if (!CheckBody(*Props[Index]))
			{
				return Finish();
			}
			DriveAcrossSeam(Index);
		}
		if (Now >= NextRebuild && (SoakSeconds > 0.0 || Rebuilds == 0))
		{
			TRACE_BOOKMARK(TEXT("VoxelLand Chaos playground rebuild"));
			if (!Test.TestTrue(TEXT("Queue collision rebuild while props use the surface"),
					Terrain.RequestChaosCollisionRebuild(Tiles)))
			{
				return Finish();
			}
			++Rebuilds;
			NextRebuild = Now + 5.0;
		}
		if (SeamCrossings == 0 || Now - SoakStarted < FMath::Max(SoakSeconds, 6.0))
		{
			return false;
		}
		Test.TestTrue(TEXT("A simulated sphere crossed the cooked tile seam"), SeamCrossings > 0);
		if (SoakSeconds >= 30.0)
		{
			Test.TestTrue(TEXT("Managed props continued to cross seams during the soak"), SeamCrossings > Props.Num());
		}
		Test.TestTrue(TEXT("The run requested at least one real rebuild"), Rebuilds > 0);
		FVoxelLandChaosCollisionOverride Disabled;
		Disabled.Mode = EVoxelLandChaosCollisionOverrideMode::Disabled;
		if (!Test.TestTrue(TEXT("Disable both logical tiles"), Terrain.SetChaosCollisionOverride(Tiles, Disabled)))
		{
			return Finish();
		}
		Phase = EPhase::Disable;
		return false;
	}

	bool UpdateDisable()
	{
		if (Terrain.GetChaosCollisionStats().ActiveBodies != 0 || HasSurfaceAtSeam())
		{
			return false;
		}
		if (!Test.TestEqual(TEXT("Disabled footprint is not reported ready"),
				Terrain.GetChaosCollisionReadiness(QueryLease), EVoxelLandChaosReadiness::Disabled))
		{
			return Finish();
		}
		for (const USphereComponent* Body : Props)
		{
			Test.TestFalse(TEXT("Disabled terrain suspends managed prop simulation"), Body->IsSimulatingPhysics());
		}
		const bool bRestored = bSlope
								 ? ConfigureSlopeTiles()
								 : Test.TestTrue(TEXT("Clear overrides to restore the same contact"),
									   Terrain.SetChaosCollisionOverride(Tiles, FVoxelLandChaosCollisionOverride{}));
		if (!bRestored)
		{
			return Finish();
		}
		Phase = EPhase::Restore;
		return false;
	}

	bool UpdateRestore()
	{
		if (Terrain.GetChaosCollisionReadiness(QueryLease) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		const FIntVector OriginAtTest(3200, -3200, 900);
		if (!Test.TestTrue(TEXT("Restored tile again blocks a real Pawn trace"), HasSurfaceAtSeam())
			|| (bSlope && !CheckSlopeTiles())
			|| !Test.TestTrue(TEXT("Rebase the physics world"), World.SetNewWorldOrigin(OriginAtTest))
			|| !Test.TestTrue(TEXT("The installed body follows its logical origin exactly once"), HasSurfaceAtSeam())
			|| !Test.TestTrue(TEXT("A newer source invalidates installed coverage"),
				Terrain.NotifyChaosCollisionSourceChanged(Tiles, 2))
			|| !Test.TestFalse(TEXT("An older source cannot replace that revision"),
				Terrain.NotifyChaosCollisionSourceChanged(Tiles, 1)))
		{
			return Finish();
		}
		Phase = EPhase::AwaitCook;
		return false;
	}

	bool UpdateAwaitCook()
	{
		if (Terrain.GetChaosCollisionStats().CookActive == 0)
		{
			return false;
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos playground disable during cook"));
		if (!Test.TestTrue(TEXT("Disable the world while an engine cook is outstanding"),
				Terrain.SetChaosTerrainCollisionEnabled(false)))
		{
			return Finish();
		}
		Test.TestTrue(TEXT("Disabled cooks remain accounted for until they drain"),
			Terrain.GetChaosCollisionStats().DetachedCooks > 0);
		Test.TestEqual(TEXT("World disable immediately rejects collision queries"),
			Terrain.GetChaosCollisionReadiness(QueryLease), EVoxelLandChaosReadiness::Disabled);
		Phase = EPhase::DrainDisabled;
		return false;
	}

	bool UpdateDrainDisabled()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (Stats.CookActive != 0 || Stats.DetachedCooks != 0 || Stats.ResidentBytes != 0)
		{
			return false;
		}
		Test.TestFalse(TEXT("An obsolete cook cannot reactivate collision while disabled"), HasSurfaceAtSeam());
		return Finish();
	}

	void SampleStats()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		PeakBodies = FMath::Max(PeakBodies, Stats.ActiveBodies);
		PeakCooks = FMath::Max(PeakCooks, Stats.CookActive);
		PeakPendingBytes = FMath::Max(PeakPendingBytes, Stats.PendingBytes);
		PeakResidentBytes = FMath::Max(PeakResidentBytes, Stats.ResidentBytes);
		PeakHolds = FMath::Max(PeakHolds, Stats.Holds);
		PeakRecoveries = FMath::Max(PeakRecoveries, Stats.Recoveries);
		for (const USphereComponent* Body : Props)
		{
			HeldSteps += Body->IsSimulatingPhysics() ? 0 : 1;
		}
	}

	bool Finish()
	{
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos playground finish"));
		Test.AddInfo(FString::Printf(
			TEXT("Chaos playground: props=%d crossings=%d fallThroughs=%d rebuilds=%d holds=%lld "
				 "heldPropSeconds=%.2f recoveries=%lld simulatedSeconds=%.2f peakBodies=%d peakCooks=%d "
				 "peakPendingBytes=%lld peakResidentBytes=%lld"),
			Props.Num(), SeamCrossings, FallThroughs, Rebuilds, PeakHolds, HeldSteps * StepSeconds, PeakRecoveries,
			PhysicsSteps * StepSeconds, PeakBodies, PeakCooks, PeakPendingBytes, PeakResidentBytes));
		for (const FGuid& Owner : Owners)
		{
			Terrain.UnregisterChaosProp(Owner);
		}
		Terrain.ReleaseChaosCollisionLease(QueryLease);
		Terrain.Shutdown();
		Test.TestEqual(
			TEXT("Shutdown clears active collision bodies"), Terrain.GetChaosCollisionStats().ActiveBodies, 0);
		Test.TestFalse(TEXT("Data-only physics never created terrain render regions"),
			bool(TActorIterator<AVoxelLandRegion>(&World)));
		World.EndPlay(EEndPlayReason::Quit);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	const bool bSlope = UsesSlope();
	TArray<FVoxelLandChunkKey> Tiles;
	TArray<USphereComponent*> Props;
	TArray<FGuid> Owners;
	TArray<int8> TravelDirection;
	TArray<int8> LastSeamSide;
	const FGuid QueryLease = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	double SoakSeconds = 0.0;
	double SoakStarted = 0.0;
	double NextRebuild = 0.0;
	FString BaselineMode;
	EPhase Phase = EPhase::Start;
	int32 Rebuilds = 0;
	int32 FallThroughs = 0;
	int32 PhysicsSteps = 0;
	int32 HeldSteps = 0;
	int32 PeakBodies = 0;
	int32 PeakCooks = 0;
	int64 PeakPendingBytes = 0;
	int64 PeakResidentBytes = 0;
	int64 PeakHolds = 0;
	int64 PeakRecoveries = 0;
	int32 SeamCrossings = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosPlaygroundTest, "VoxelLand.Collision.Chaos.Playground",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosPlaygroundTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	const FVoxelLandChaosPlaygroundCommand::FHeightFixture::FGeneratorPtr Generator =
		Async(EAsyncExecution::Thread, &FVoxelLandChaosPlaygroundCommand::MakeGenerator).Get();
	if (!TestTrue(TEXT("Construct the deterministic height fixture"), Generator.IsValid()))
	{
		return false;
	}

	FWorldInitializationValues Init;
	Init.CreatePhysicsScene(true).ShouldSimulatePhysics(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* const World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Create the test world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* const Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("Create data-only voxel terrain"), Terrain))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Terrain->bDataOnly = true;
	Terrain->WorkerCount = 1;
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	Terrain->Initialize(Generator);
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandChaosPlaygroundCommand(*this, *World, *Terrain));
	return true;
}

#endif
