#include "World/Demo/VoxelLandChaosDemo.h"

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShowFlags.h"
#include "UnrealClient.h"
#endif

AVoxelLandChaosDemo::AVoxelLandChaosDemo()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PlaygroundRoot")));
}

void AVoxelLandChaosDemo::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bSmokeRequested = GetWorld()->WorldType == EWorldType::Game && !GIsEditor
				   && FParse::Param(FCommandLine::Get(), TEXT("VoxelLandChaosPlaygroundSmoke"));
	bCaptureSmokeViews = bSmokeRequested && FParse::Param(FCommandLine::Get(), TEXT("VoxelLandChaosPlaygroundCapture"));
#endif
	OnInitializationFinished.AddDynamic(this, &AVoxelLandChaosDemo::HandleTerrainInitialization);
	StartupTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AVoxelLandChaosDemo::StartTerrain);
}

void AVoxelLandChaosDemo::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(StartupTimer);
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(SmokeTimer);
#endif
	OnInitializationFinished.RemoveDynamic(this, &AVoxelLandChaosDemo::HandleTerrainInitialization);
	ClearPlaygroundProps();
	Super::EndPlay(Reason);
}

void AVoxelLandChaosDemo::StartTerrain()
{
	bDataOnly = bDataOnly || IsRunningDedicatedServer() || !FApp::CanEverRender();
	if (TerrainConfig && InitializeFromConfig(TerrainConfig, FIntVector::ZeroValue))
	{
		return;
	}
	if (!TerrainConfig)
	{
		LastInitializationError = TEXT("Assign a World Config asset to the Chaos playground.");
	}
	HandleTerrainInitialization(false);
}

void AVoxelLandChaosDemo::HandleTerrainInitialization(const bool bSuccess)
{
	bTerrainReady = bSuccess;
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *GetName(), *LastInitializationError);
	}
	const bool bOptedIn =
		bSuccess && TerrainConfig && (bEnableChaosForPlayground || TerrainConfig->ChaosCollision.bEnabled);
	if (!bOptedIn || !SetPlaygroundCollisionEnabled(true))
	{
#if !UE_BUILD_SHIPPING
		if (bSmokeRequested)
		{
			FinishSmoke(false);
		}
#endif
		return;
	}

	const int32 DropCount = FMath::Clamp(InitialPropCount, 0, MaxPlaygroundProps);
	const FVector FirstDropPosition = GetActorLocation() + FirstDropOffset;
	for (int32 Index = 0; Index < DropCount; ++Index)
	{
		if (!DropPlaygroundProp(FirstDropPosition + FVector(Index * SpawnSpacingCm, 0.0, 0.0)))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: could not register playground prop %d."), *GetName(), Index);
			break;
		}
	}
#if !UE_BUILD_SHIPPING
	if (bSmokeRequested)
	{
		StartSmoke();
	}
#endif
}

bool AVoxelLandChaosDemo::SetPlaygroundCollisionEnabled(const bool bEnabled)
{
	if (!bTerrainReady || !SetChaosTerrainCollisionEnabled(bEnabled))
	{
		return false;
	}
	bCollisionEnabled = bEnabled;
	return true;
}

AActor* AVoxelLandChaosDemo::DropPlaygroundProp(const FVector WorldPosition)
{
	UWorld* const World = GetWorld();
	if (!bCollisionEnabled || !World || World->bIsTearingDown || WorldPosition.ContainsNaN()
		|| Props.Num() >= MaxPlaygroundProps || !FMath::IsFinite(PropRadiusCm))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* const Prop = World->SpawnActor<AActor>(WorldPosition, FRotator::ZeroRotator, SpawnParameters);
	if (!Prop)
	{
		return nullptr;
	}

	USphereComponent* const Body = NewObject<USphereComponent>(Prop);
	Prop->SetRootComponent(Body);
	Prop->AddInstanceComponent(Body);
	const float Radius = FMath::Clamp(PropRadiusCm, 5.0f, 100.0f);
	Body->InitSphereRadius(Radius);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetGenerateOverlapEvents(false);
	Body->SetWorldLocation(WorldPosition);
	Body->RegisterComponent();
	Body->SetSimulatePhysics(true);

	FGuid Consumer;
	if (!RegisterChaosProp(*Body, Consumer))
	{
		Prop->Destroy();
		return nullptr;
	}
	Props.Add(Consumer, Prop);

#if !UE_SERVER
	if (!bDataOnly)
	{
		UStaticMesh* const Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (Sphere)
		{
			constexpr float AssetRadiusCm = 50.0f;
			UStaticMeshComponent* const Visual = NewObject<UStaticMeshComponent>(Prop);
			Prop->AddInstanceComponent(Visual);
			Visual->SetupAttachment(Body);
			Visual->SetMobility(EComponentMobility::Movable);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Visual->SetStaticMesh(Sphere);
			Visual->SetRelativeScale3D(FVector(Radius / AssetRadiusCm));
			Visual->RegisterComponent();
		}
	}
#endif
	return Prop;
}

void AVoxelLandChaosDemo::ClearPlaygroundProps()
{
	TMap<FGuid, TWeakObjectPtr<AActor>> Existing = MoveTemp(Props);
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Entry : Existing)
	{
		UnregisterChaosProp(Entry.Key);
		if (AActor* const Prop = Entry.Value.Get())
		{
			Prop->Destroy();
		}
	}
}

#if !UE_BUILD_SHIPPING
void AVoxelLandChaosDemo::StartSmoke()
{
	const int32 Expected = FMath::Clamp(InitialPropCount, 1, MaxPlaygroundProps);
	if (Props.Num() != Expected)
	{
		FinishSmoke(false);
		return;
	}
	SmokeStartTime = FPlatformTime::Seconds();
	constexpr float SampleIntervalSeconds = 0.125f;
	GetWorldTimerManager().SetTimer(SmokeTimer, this, &AVoxelLandChaosDemo::CheckSmoke, SampleIntervalSeconds, true);
}

void AVoxelLandChaosDemo::CheckSmoke()
{
	constexpr double TimeoutSeconds = 20.0;
	constexpr double SampleLengthCm = 100.0;
	constexpr double AllowedHeightDifferenceCm = 20.0;
	constexpr double SettledSpeedCmPerSecond = 15.0;
	if (FPlatformTime::Seconds() - SmokeStartTime > TimeoutSeconds)
	{
		FinishSmoke(false);
		return;
	}
	if (SmokeCaptureStep > 0)
	{
		CaptureSmokeViews();
		return;
	}
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Entry : Props)
	{
		AActor* const Prop = Entry.Value.Get();
		USphereComponent* const Sphere = Prop ? Cast<USphereComponent>(Prop->GetRootComponent()) : nullptr;
		if (!Sphere || !Sphere->IsSimulatingPhysics()
			|| FMath::Abs(Sphere->GetPhysicsLinearVelocity().Z) > SettledSpeedCmPerSecond)
		{
			return;
		}
		FHitResult Hit;
		const FVector Center = Sphere->GetComponentLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelLandChaosPlaygroundSmoke), true, Prop);
		const FVector End = Center - FVector(0.0, 0.0, Sphere->GetScaledSphereRadius() + SampleLengthCm);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Center, End, ECC_Pawn, Params)
			|| !Cast<UVoxelLandChaosTerrainComponent>(Hit.GetComponent())
			|| FMath::Abs((Center.Z - Sphere->GetScaledSphereRadius()) - Hit.ImpactPoint.Z) > AllowedHeightDifferenceCm)
		{
			return;
		}
	}
	if (bCaptureSmokeViews)
	{
		CaptureSmokeViews();
	}
	else
	{
		FinishSmoke(true);
	}
}

void AVoxelLandChaosDemo::CaptureSmokeViews()
{
	UGameViewportClient* const Viewport = GetWorld()->GetGameViewport();
	if (!Viewport || !Viewport->GetGameViewport())
	{
		FinishSmoke(false);
		return;
	}

	switch (SmokeCaptureStep)
	{
		case 0:
		{
			const FString Folder = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("VoxelLandChaosPlayground"));
			if (!IFileManager::Get().MakeDirectory(*Folder, true))
			{
				FinishSmoke(false);
				return;
			}
			const uint32 Process = FPlatformProcess::GetCurrentProcessId();
			SmokeCollisionImage = Folder / FString::Printf(TEXT("PlayerCollision-%u.png"), Process);
			SmokeLitImage = Folder / FString::Printf(TEXT("Lit-%u.png"), Process);
			Viewport->SetViewMode(VMI_CollisionPawn, GLog);
			if (!Viewport->GetEngineShowFlags()->CollisionPawn)
			{
				FinishSmoke(false);
				return;
			}
			break;
		}
		case 1:
			FScreenshotRequest::RequestScreenshot(SmokeCollisionImage, false, false);
			break;
		case 2:
			if (!FPaths::FileExists(SmokeCollisionImage))
			{
				return;
			}
			Viewport->SetViewMode(VMI_Lit, GLog);
			if (Viewport->GetEngineShowFlags()->CollisionPawn)
			{
				FinishSmoke(false);
				return;
			}
			break;
		case 3:
			FScreenshotRequest::RequestScreenshot(SmokeLitImage, false, false);
			break;
		case 4:
			if (FPaths::FileExists(SmokeLitImage))
			{
				UE_LOG(LogTemp, Display, TEXT("VoxelLand Chaos Playground captures: %s | %s"), *SmokeCollisionImage,
					*SmokeLitImage);
				FinishSmoke(true);
			}
			return;
		default:
			checkNoEntry();
			return;
	}
	++SmokeCaptureStep;
}

void AVoxelLandChaosDemo::FinishSmoke(const bool bPassed)
{
	GetWorldTimerManager().ClearTimer(SmokeTimer);
	const FVoxelLandChaosStats Stats = GetChaosCollisionStats();
	UE_LOG(LogTemp, Display, TEXT("VoxelLand Chaos Playground Smoke: %s props=%d activeBodies=%d holds=%lld"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"), Props.Num(), Stats.ActiveBodies, Stats.Holds);
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}
#endif
