#if WITH_DEV_AUTOMATION_TESTS

#include "../../Worlds/PCG/VoxelLandPCGForestExample.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "VoxelLandPCGCell.h"
#include "VoxelLandPCGComponent.h"
#include "VoxelLandPCGWorld.h"
#include "Worlds/VoxelLandTerrainPreview.h"
#include "Camera/CameraActor.h"
#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(...)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PCGGraph.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandPCGForestIntegrationCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandPCGForestIntegrationCommand(FAutomationTestBase& InTest, const bool bInEditor, const bool bInInfinite)
		: Test(InTest)
		, bEditor(bInEditor)
		, bInfinite(bInInfinite)
	{
	}

	~FVoxelLandPCGForestIntegrationCommand() override { CloseWorld(); }

	bool Update() override
	{
		if (!World)
		{
			return !OpenWorld();
		}
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		const double Deadline = bStress ? 120.0 : 50.0;
		if (FPlatformTime::Seconds() - Started > Deadline)
		{
			Test.AddError(FString::Printf(TEXT("Forest fixture did not reach expected stage %d. PCG: %s. Terrain: %s."),
				static_cast<int32>(Phase), *Placement->PlacementStatus,
				Preview ? *Preview->PreviewStatus.ToString() : *Runtime->LastInitializationError));
			return true;
		}

		if (bEditor)
		{
			return AdvanceEditor();
		}
		return AdvanceRuntime();
	}

private:
	enum class EPhase : uint8
	{
		Placement,
		Elevated,
		Away,
		Return,
		Cleanup
	};
	static constexpr double CenterSettleSeconds = 0.45;
	double DestinationHoldSeconds = 3.0;
	static constexpr const TCHAR* TemplatePath = TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills");

	bool OpenWorld()
	{
		const UVoxelLandWorldConfig* Template = LoadObject<UVoxelLandWorldConfig>(nullptr, TemplatePath);
		if (!Test.TestNotNull(TEXT("Load known terrain fixture"), Template))
		{
			return false;
		}
		Config.Reset(DuplicateObject<UVoxelLandWorldConfig>(Template, GetTransientPackage()));
		Mask.Reset(NewObject<UVoxelLandTerrainGraph>(GetTransientPackage()));
		Graph.Reset(NewObject<UPCGGraph>(GetTransientPackage()));
		if (!Test.TestTrue(TEXT("Wire the editable forest graph"),
				FVoxelLandPCGForestExample::BuildGraph(*Graph, FVoxelLandPCGForestExample::BuildMask(*Mask))))
		{
			return false;
		}
		Config->MaskGraph = Mask.Get();
		Config->WorldExtent = bInfinite ? EVoxelLandWorldExtent::Infinite : EVoxelLandWorldExtent::Finite;
		Config->bGlobalErosion = false;
		Config->ChaosCollision.bEnabled = false;

		const EWorldType::Type Type = bEditor ? EWorldType::Editor : EWorldType::Game;
		const UWorld::InitializationValues Init = UWorld::InitializationValues()
													  .CreatePhysicsScene(false)
													  .ShouldSimulatePhysics(false)
													  .CreateNavigation(false)
													  .CreateAISystem(false);
		World = UWorld::CreateWorld(Type, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Create an isolated forest world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
		Started = FPlatformTime::Seconds();
		return bEditor ? StartEditor() : StartRuntime();
	}

	bool StartEditor()
	{
		Preview = World->SpawnActor<AVoxelLandTerrainPreview>();
		if (!Test.TestNotNull(TEXT("Spawn the authored editor preview actor"), Preview))
		{
			return false;
		}
		Placement = Preview->Placement;
		Preview->WorldConfig = Config.Get();
		Preview->PreviewExtentCm = FVector(3600.0, 3600.0, 3200.0);
		if (!ConfigureLayer())
		{
			return false;
		}
		Preview->GeneratePreview();
		return true;
	}

	bool StartRuntime()
	{
		World->InitializeActorsForPlay(FURL());
		Runtime = World->SpawnActor<AVoxelLandPCGWorld>();
		Player = World->SpawnActor<APlayerController>();
		Camera = World->SpawnActor<ACameraActor>(FVector(1600.0, 1600.0, 1200.0), FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Spawn runtime terrain"), Runtime)
			|| !Test.TestNotNull(TEXT("Spawn local streaming controller"), Player)
			|| !Test.TestNotNull(TEXT("Spawn camera"), Camera))
		{
			return false;
		}
		Runtime->Config = Config.Get();
		Runtime->WorkerCount = 2;
		Placement = Runtime->Placement;
		if (!ConfigureLayer())
		{
			return false;
		}
		FVoxelLandPCGLayer& Layer = Placement->Layers[0];
		Layer.GenerationRadiusCm = 4500.0;
		Layer.RetirementMarginCm = 3200.0;
		Placement->MaxResidentCells = 48;
		ConfigureStress(Layer);
		Player->SetAsLocalPlayerController();
		World->GetWorldSettings()->NotifyBeginPlay();
		Player->SetViewTarget(Camera);
		return true;
	}

	void ConfigureStress(FVoxelLandPCGLayer& Layer)
	{
		bStress = bInfinite && FParse::Param(FCommandLine::Get(), TEXT("VoxelLandPCGForestStress"));
		if (!bStress)
		{
			return;
		}
		double Radius = 56000.0;
		double Travel = 120000.0;
		int32 Cells = 96;
		int32 CellChunks = Placement->CellChunks;
		int32 VerticalCells = Placement->VerticalCells;
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressRadiusCm="), Radius);
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressTravelCm="), Travel);
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressCells="), Cells);
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressCellChunks="), CellChunks);
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressVerticalCells="), VerticalCells);
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressHoldSeconds="), DestinationHoldSeconds);
		DestinationHoldSeconds = FMath::IsFinite(DestinationHoldSeconds)
			? FMath::Clamp(DestinationHoldSeconds, 1.0, 30.0)
			: 3.0;
		int32 Jobs = Placement->MaxCellJobs;
		FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestStressJobs="), Jobs);
		Placement->MaxCellJobs = FMath::Clamp(Jobs, 1, 12);
		Placement->MaxPendingFieldMiB = 96;
		Layer.GenerationRadiusCm = FMath::IsFinite(Radius) ? FMath::Clamp(Radius, 4500.0, 200000.0) : 56000.0;
		Layer.VisibilityDistanceCm = FMath::Max(Layer.VisibilityDistanceCm, Layer.GenerationRadiusCm);
		Placement->MaxResidentCells = FMath::Clamp(Cells, 48, 512);
		Placement->CellChunks = FMath::Clamp(CellChunks, 1, 4);
		Placement->VerticalCells = FMath::Clamp(VerticalCells, 0, 4);
		TravelCm = FMath::Max(FMath::IsFinite(Travel) ? FMath::Min(Travel, 1000000.0) : 120000.0,
			Layer.GenerationRadiusCm + Layer.RetirementMarginCm + 12800.0);
	}

	bool ConfigureLayer()
	{
		if (!FVoxelLandPCGForestExample::ConfigureLayer(*Placement, *Graph))
		{
			Test.AddError(TEXT("Could not configure the forest marker layer."));
			return false;
		}
		FString SkeletalPath;
		if (FParse::Value(FCommandLine::Get(), TEXT("VoxelLandPCGForestSkeletalMesh="), SkeletalPath))
		{
			FVoxelLandPCGLayer& Layer = Placement->Layers[0];
			Layer.SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalPath);
			Layer.StaticMesh = nullptr;
			return Test.TestNotNull(TEXT("Load the requested skeletal reference asset"), Layer.SkeletalMesh.Get());
		}
		return true;
	}

	bool AdvanceEditor()
	{
		if (!Preview->GetPreviewWorld() || Placement->GetVisibleInstanceCount() == 0)
		{
			return false;
		}
		Test.TestFalse(TEXT("Forest is generated outside Play"), World->HasBegunPlay());
		Test.TestTrue(TEXT("Editor produced cosmetic instances"), Placement->GetVisibleInstanceCount() > 0);
		Preview->ClearPreview();
		Test.TestNull(TEXT("Clear removes the staged terrain world"), Preview->GetPreviewWorld());
		Test.TestEqual(TEXT("Clear removes published forest cells"), Placement->GetVisibleCellCount(), 0);
		return true;
	}

	void CaptureCenter(TArray<FIntVector>& Out) const
	{
		for (TActorIterator<AVoxelLandPCGCell> It(World); It; ++It)
		{
			const FVector Center = It->GetActorLocation();
			if (Center.X < -3200.0 || Center.X > 6400.0 || Center.Y < -3200.0 || Center.Y > 6400.0)
			{
				continue;
			}
			TInlineComponentArray<UInstancedStaticMeshComponent*> Components(*It);
			for (const UInstancedStaticMeshComponent* Component : Components)
			{
				for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
				{
					FTransform Transform;
					if (!Component->GetInstanceTransform(Index, Transform, true))
					{
						continue;
					}
					AppendCenterPoint(Transform.GetLocation(), Out);
				}
			}
			TInlineComponentArray<UInstancedSkinnedMeshComponent*> Skinned(*It);
			for (const UInstancedSkinnedMeshComponent* Component : Skinned)
			{
				for (const FSkinnedMeshInstanceData& Instance : Component->GetInstanceData())
				{
					const FVector Position =
						Component->GetComponentTransform().TransformPosition(FVector(Instance.Transform.GetLocation()));
					AppendCenterPoint(Position, Out);
				}
			}
		}
		Out.Sort([](const FIntVector& A, const FIntVector& B) {
			return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
		});
	}

	static void AppendCenterPoint(const FVector& Position, TArray<FIntVector>& Out)
	{
		if (Position.X >= 0.0 && Position.X < 3200.0 && Position.Y >= 0.0 && Position.Y < 3200.0)
		{
			Out.Emplace(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y), FMath::RoundToInt(Position.Z));
		}
	}

	bool IsSettledCenter(const TArray<FIntVector>& Current)
	{
		const double Now = FPlatformTime::Seconds();
		if (Current != LatestCenter)
		{
			LatestCenter = Current;
			LastCenterChange = Now;
		}
		return !Current.IsEmpty() && Now - LastCenterChange >= CenterSettleSeconds;
	}

	bool ObserveDestination()
	{
		const FVector Destination(TravelCm, 1600.0, 1200.0);
		FVector ActualView;
		FRotator Rotation;
		Player->GetPlayerViewPoint(ActualView, Rotation);
		if (FVector::Dist2D(ActualView, Destination) > 50.0)
		{
			DestinationReadyAt = 0.0;
			return false;
		}
		const double RadiusCm = Placement->Layers[0].GenerationRadiusCm;
		int32 Cells = 0;
		int32 Instances = 0;
		double ReachCm = 0.0;
		for (TActorIterator<AVoxelLandPCGCell> It(World); It; ++It)
		{
			const double Reach = FVector::Dist2D(It->GetActorLocation(), ActualView);
			if (Reach <= RadiusCm + 3200.0 * Placement->CellChunks && It->GetInstanceCount() > 0)
			{
				Cells += 1;
				Instances += It->GetInstanceCount();
				ReachCm = FMath::Max(ReachCm, Reach);
			}
		}
		if (Cells < 4)
		{
			DestinationReadyAt = 0.0;
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (DestinationReadyAt == 0.0)
		{
			DestinationReadyAt = Now;
		}
		if (Now - DestinationReadyAt < DestinationHoldSeconds)
		{
			return false;
		}
		Test.AddInfo(FString::Printf(
			TEXT(
				"Forest stress travelled %.0f cm with %.0f cm requested radius, %d resident cell limit, %d chunks per cell axis, %d vertical cell padding, %d destination cells, %d instances, %.0f cm farthest published cell center after %.1fs"),
			FVector::Dist2D(ActualView, InitialView), RadiusCm, Placement->MaxResidentCells, Placement->CellChunks,
			Placement->VerticalCells, Cells, Instances, ReachCm, Now - DestinationReadyAt));
		return true;
	}

	bool AdvanceRuntime()
	{
		TArray<FIntVector> Current;
		CaptureCenter(Current);
		const bool bSettled = IsSettledCenter(Current);
		switch (Phase)
		{
			case EPhase::Placement:
				if (bSettled)
				{
					Baseline = MoveTemp(Current);
					FRotator Rotation;
					Player->GetPlayerViewPoint(InitialView, Rotation);
					TSet<FIntVector> Unique;
					Unique.Append(Baseline);
					Test.TestEqual(TEXT("One owner per canonical forest point"), Unique.Num(), Baseline.Num());
					Camera->SetActorLocation(FVector(1600.0, 1600.0, 18000.0));
					ElevatedAt = FPlatformTime::Seconds();
					Phase = EPhase::Elevated;
				}
				return false;
			case EPhase::Elevated:
			{
				FVector View;
				FRotator Rotation;
				Player->GetPlayerViewPoint(View, Rotation);
				if (View.Z >= 15000.0 && FPlatformTime::Seconds() - ElevatedAt >= 0.8)
				{
					Test.TestTrue(
						FString::Printf(TEXT("Raised camera preserves center ground placement (%d before, %d after)"),
							Baseline.Num(), Current.Num()),
						Current == Baseline);
					Camera->SetActorLocation(FVector(TravelCm, 1600.0, 1200.0));
					Phase = EPhase::Away;
				}
				return false;
			}
			case EPhase::Away:
				if (Current.IsEmpty() && (!bStress || ObserveDestination()))
				{
					Camera->SetActorLocation(FVector(1600.0, 1600.0, 1200.0));
					Phase = EPhase::Return;
				}
				return false;
			case EPhase::Return:
				if (bSettled)
				{
					Test.TestTrue(TEXT("A revisited cell regenerates the same forest points"), Current == Baseline);
					Runtime->Shutdown();
					Phase = EPhase::Cleanup;
				}
				return false;
			case EPhase::Cleanup:
				if (Placement->GetVisibleCellCount() != 0)
				{
					return false;
				}
				Test.TestEqual(TEXT("Source shutdown retires cosmetic instances without a preview clear"),
					Placement->GetVisibleInstanceCount(), 0);
				return true;
		}
		return true;
	}

	void CloseWorld()
	{
		if (!World)
		{
			return;
		}
		if (Placement)
		{
			Placement->ClearPreview();
		}
		if (World->HasBegunPlay())
		{
			World->EndPlay(EEndPlayReason::Destroyed);
		}
		World->DestroyWorld(false);
		if (GEngine)
		{
			GEngine->DestroyWorldContext(World);
		}
		World->RemoveFromRoot();
		World = nullptr;
	}

	FAutomationTestBase& Test;
	const bool bEditor;
	const bool bInfinite;
	UWorld* World = nullptr;
	UVoxelLandPCGComponent* Placement = nullptr;
	AVoxelLandTerrainPreview* Preview = nullptr;
	AVoxelLandPCGWorld* Runtime = nullptr;
	ACameraActor* Camera = nullptr;
	APlayerController* Player = nullptr;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TStrongObjectPtr<UVoxelLandTerrainGraph> Mask;
	TStrongObjectPtr<UPCGGraph> Graph;
	TArray<FIntVector> Baseline;
	TArray<FIntVector> LatestCenter;
	double Started = 0.0;
	double ElevatedAt = 0.0;
	double LastCenterChange = 0.0;
	double DestinationReadyAt = 0.0;
	double TravelCm = 30000.0;
	FVector InitialView = FVector::ZeroVector;
	bool bStress = false;
	EPhase Phase = EPhase::Placement;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPCGForestCreateExamples, "VoxelLand.PCG.Forest.CreateExamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandPCGForestCreateExamples::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandWritePCGForestExamples")))
	{
		AddInfo(TEXT("Supply -VoxelLandWritePCGForestExamples to create example packages in a clean checkout."));
		return true;
	}
	FString Error;
	if (!FVoxelLandPCGForestExample::WriteAssets(Error))
	{
		AddError(Error);
		return false;
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FVoxelLandPCGForestIntegration, "VoxelLand.PCG.Forest.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FVoxelLandPCGForestIntegration::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("EditorPreview"), TEXT("FiniteRuntime"), TEXT("InfiniteRuntime")};
	Commands = Names;
}

bool FVoxelLandPCGForestIntegration::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Run without NullRHI to verify actual editor and runtime cosmetic publication."));
		return true;
	}
	if (!TestNotNull(TEXT("Editor engine"), GEngine))
	{
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVoxelLandPCGForestIntegrationCommand>(
		*this, Parameters == TEXT("EditorPreview"), Parameters == TEXT("InfiniteRuntime")));
	return true;
}

#endif
