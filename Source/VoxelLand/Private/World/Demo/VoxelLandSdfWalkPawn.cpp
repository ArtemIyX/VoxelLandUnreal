#include "World/Demo/VoxelLandSdfWalkPawn.h"

#include "../VoxelLandWorldCoordinates.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/Collision/VoxelLandWalkMotor.h"
#include "World/VoxelLandWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "ProfilingDebugging/CountersTrace.h"

TRACE_DECLARE_INT_COUNTER(VoxelLandWalkGrounded, TEXT("VoxelLand/WalkGrounded"));
TRACE_DECLARE_INT_COUNTER(VoxelLandWalkBlocked, TEXT("VoxelLand/WalkBlocked"));

AVoxelLandSdfWalkPawn::AVoxelLandSdfWalkPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	bUseControllerRotationYaw = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(float(WalkSettings.RadiusCm), float(WalkSettings.HalfHeightCm));
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Capsule->SetGenerateOverlapEvents(false);
	Capsule->SetCanEverAffectNavigation(false);
	SetRootComponent(Capsule);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Capsule);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, BaseEyeHeight));
	Camera->bUsePawnControlRotation = true;
}

void AVoxelLandSdfWalkPawn::BeginPlay()
{
	Super::BeginPlay();
	DemandOwner = FGuid::NewGuid();
}

void AVoxelLandSdfWalkPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearCollisionDemand();
	Super::EndPlay(Reason);
}

void AVoxelLandSdfWalkPawn::UnPossessed()
{
	ClearCollisionDemand();
	Super::UnPossessed();
}

void AVoxelLandSdfWalkPawn::ClearCollisionDemand()
{
	if (AVoxelLandWorld* Terrain = ActiveTerrain.Get())
	{
		Terrain->ReleaseCollisionDemand(DemandOwner);
	}
	ActiveTerrain.Reset();
	ActiveEpoch.Reset();
	WalkState = {};
	AccumulatedSeconds = 0.0;
	bIsMotionBlocked = false;
}

FVector2d AVoxelLandSdfWalkPawn::ReadMovementInput(const APlayerController& Player) const
{
	const double Forward = double(Player.IsInputKeyDown(EKeys::W)) - double(Player.IsInputKeyDown(EKeys::S));
	const double Right = double(Player.IsInputKeyDown(EKeys::D)) - double(Player.IsInputKeyDown(EKeys::A));
	const double Yaw = FMath::DegreesToRadians(Player.GetControlRotation().Yaw);
	double SinYaw;
	double CosYaw;
	FMath::SinCos(&SinYaw, &CosYaw, Yaw);
	return FVector2d(Forward * CosYaw - Right * SinYaw, Forward * SinYaw + Right * CosYaw);
}

FBox AVoxelLandSdfWalkPawn::MakeDemandBounds(const FVector& LocalCenter) const
{
	const double Move = WalkSettings.SpeedCmPerSecond * FVoxelLandWalkSettings::FixedStepSeconds;
	const double Fall = WalkSettings.TerminalSpeedCmPerSecond * FVoxelLandWalkSettings::FixedStepSeconds;
	const double Radius = WalkSettings.RadiusCm + WalkSettings.SkinCm + Move;
	const double HalfHeight = WalkSettings.HalfHeightCm + WalkSettings.SkinCm;
	// Include slope/slide displacement, the optional step-up and the complete ground probe.
	const double Above = HalfHeight + FMath::Max(WalkSettings.StepHeightCm, Move);
	const double Below = HalfHeight + Move + Fall + WalkSettings.FloorProbeCm;
	return FBox(LocalCenter - FVector(Radius, Radius, Below), LocalCenter + FVector(Radius, Radius, Above));
}

bool AVoxelLandSdfWalkPawn::SimulateStep(AVoxelLandWorld& Terrain, const FVector2d& Input)
{
	const FVector LocalCenter = GetActorLocation();
	FVoxelLandCollisionWorldView View;
	if (!Terrain.UpdateCollisionDemandForBounds(DemandOwner, MakeDemandBounds(LocalCenter))
		|| !Terrain.CaptureCollisionView(DemandOwner, View) || !View.Field)
	{
		return false;
	}

	const uint64 Epoch = View.GetConfigurationEpoch();
	if (!ActiveEpoch.IsSet() || ActiveEpoch.GetValue() != Epoch)
	{
		WalkState = {};
		ActiveEpoch = Epoch;
	}

	FVoxelLandWalkState Next = WalkState;
	if (!VoxelLandWorldCoordinates::ToGlobal(LocalCenter, View.OriginLocation, Next.Center)
		|| !FVoxelLandWalkMotor::Step(*View.Field, WalkSettings, Input, Next) || !Terrain.IsCollisionViewCurrent(View))
	{
		return false;
	}
	FVector NextLocal;
	if (!VoxelLandWorldCoordinates::ToLocal(Next.Center, View.OriginLocation, NextLocal)
		|| (!NextLocal.Equals(LocalCenter)
			&& !SetActorLocation(NextLocal, false, nullptr, ETeleportType::TeleportPhysics)))
	{
		return false;
	}
	WalkState = Next;
	return true;
}

void AVoxelLandSdfWalkPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* Player = Cast<APlayerController>(GetController());
	if (!Player || !Player->IsLocalController() || GetNetMode() != NM_Standalone)
	{
		ClearCollisionDemand();
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	Player->GetInputMouseDelta(MouseX, MouseY);
	AddControllerYawInput(MouseX);
	AddControllerPitchInput(-MouseY);

	AVoxelLandWorld* Terrain = TerrainWorld.Get();
	if (ActiveTerrain.Get() != Terrain)
	{
		ClearCollisionDemand();
	}
	if (!IsValid(Terrain) || Terrain->GetWorld() != GetWorld())
	{
		bIsMotionBlocked = true;
		return;
	}
	ActiveTerrain = Terrain;

	const double StepSeconds = FVoxelLandWalkSettings::FixedStepSeconds;
	AccumulatedSeconds += FMath::Clamp(double(DeltaSeconds), 0.0, MaxStepsPerTick * StepSeconds);
	AccumulatedSeconds = FMath::Min(AccumulatedSeconds, MaxStepsPerTick * StepSeconds);
	const FVector2d Input = ReadMovementInput(*Player);
	while (AccumulatedSeconds >= StepSeconds)
	{
		AccumulatedSeconds -= StepSeconds;
		bIsMotionBlocked = !SimulateStep(*Terrain, Input);
		TRACE_COUNTER_SET(VoxelLandWalkGrounded, WalkState.bIsGrounded ? 1 : 0);
		TRACE_COUNTER_SET(VoxelLandWalkBlocked, bIsMotionBlocked ? 1 : 0);
		if (bIsMotionBlocked)
		{
			// Discard withheld time so chunk arrival cannot kick the pawn forward.
			AccumulatedSeconds = 0.0;
			break;
		}
	}
}
