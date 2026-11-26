#include "World/Demo/VoxelLandClipmapDemo.h"

#if !UE_BUILD_SHIPPING && !UE_SERVER

#include "World/VoxelLandWorldCoordinates.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"

class FVoxelLandClipmapFlightRoute final
{
public:
	static constexpr double ViewRadiusCm = 300000.0;
	static constexpr int32 MaxLeaves = 1024;
	static constexpr double CruiseSpeedCmPerSecond = 18000.0;
	static constexpr double SampleIntervalSeconds = 0.5;
	static constexpr double ProbeOffsetCm = 137.0;
	static constexpr double CameraPitch = -12.0;
	static constexpr double StopsCm[] = {0.0, 55000.0, 110000.0, 165000.0, 192000.0, 207000.0, 215500.0, 221000.0, 0.0};
};

void AVoxelLandClipmapDemo::ConfigureFlight()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	bFlightRequested = !bDataOnly && FParse::Param(CommandLine, TEXT("VoxelLandClipmapFlight"));
	if (!bFlightRequested)
	{
		return;
	}

	M7MaxLOD = 5;
	M7HorizontalRadiusCm = FVoxelLandClipmapFlightRoute::ViewRadiusCm;
	M7MaxLeaves = FVoxelLandClipmapFlightRoute::MaxLeaves;
	FParse::Value(CommandLine, TEXT("VoxelLandClipmapFlightRadiusCm="), M7HorizontalRadiusCm);
	if (!FMath::IsFinite(M7HorizontalRadiusCm))
	{
		M7HorizontalRadiusCm = FVoxelLandClipmapFlightRoute::ViewRadiusCm;
	}
	M7HorizontalRadiusCm = FMath::Max(1.0, M7HorizontalRadiusCm);
	FParse::Value(CommandLine, TEXT("VoxelLandClipmapFlightLeaves="), M7MaxLeaves);
	M7MaxLeaves = FMath::Clamp(M7MaxLeaves, 8, FVoxelLandClipmapFlightRoute::MaxLeaves);
	FParse::Value(CommandLine, TEXT("VoxelLandClipmapFlightHold="), FlightHoldSeconds);
	FlightHoldSeconds = FMath::IsFinite(FlightHoldSeconds) ? FMath::Clamp(FlightHoldSeconds, 1.0, 60.0) : 5.0;
}

void AVoxelLandClipmapDemo::Tick(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	if (bFlightReady && World && !World->bIsTearingDown)
	{
		if (!FlightCamera.IsValid())
		{
			StartFlight();
		}
		if (FlightCamera.IsValid())
		{
			AdvanceFlight();
		}
	}
	Super::Tick(DeltaSeconds);
}

void AVoxelLandClipmapDemo::StartFlight()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller || !Controller->GetViewTarget())
	{
		return;
	}

	FVector LocalOrigin;
	FRotator OriginalRotation;
	Controller->GetPlayerViewPoint(LocalOrigin, OriginalRotation);
	if (!VoxelLandWorldCoordinates::ToGlobal(LocalOrigin, World->OriginLocation, FlightOrigin))
	{
		bFlightReady = false;
		return;
	}

	FActorSpawnParameters Parameters;
	Parameters.Owner = this;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator Rotation(FVoxelLandClipmapFlightRoute::CameraPitch, 0.0, 0.0);
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(LocalOrigin, Rotation, Parameters);
	if (!Camera)
	{
		bFlightReady = false;
		return;
	}

	FlightPreviousView = Controller->GetViewTarget();
	FlightCamera = Camera;
	Controller->SetViewTarget(Camera);
	FlightStartedAt = FPlatformTime::Seconds();
	NextFlightSampleAt = FlightStartedAt;
	constexpr int32 StopCount = UE_ARRAY_COUNT(FVoxelLandClipmapFlightRoute::StopsCm);
	FlightProbes.Reset(StopCount - 1);
	for (int32 Index = 0; Index < StopCount - 1; ++Index)
	{
		FlightProbes.Emplace(
			FlightOrigin.X + FVoxelLandClipmapFlightRoute::StopsCm[Index] + FVoxelLandClipmapFlightRoute::ProbeOffsetCm,
			FlightOrigin.Y + FVoxelLandClipmapFlightRoute::ProbeOffsetCm);
	}
	TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight start"));
	UE_LOG(LogTemp, Display, TEXT("ClipmapFlight start origin=%s radiusCm=%.0f leaves=%d hold=%.1fs fixedProbes=%d"),
		*FlightOrigin.ToCompactString(), M7HorizontalRadiusCm, M7MaxLeaves, FlightHoldSeconds, FlightProbes.Num());
}

double AVoxelLandClipmapDemo::GetFlightOffsetCm(double Elapsed, int32& OutCheckpoint, bool& bOutComplete) const
{
	OutCheckpoint = INDEX_NONE;
	bOutComplete = false;
	constexpr int32 StopCount = UE_ARRAY_COUNT(FVoxelLandClipmapFlightRoute::StopsCm);
	for (int32 Index = 0; Index < StopCount; ++Index)
	{
		const double From = FVoxelLandClipmapFlightRoute::StopsCm[Index];
		if (Elapsed < FlightHoldSeconds)
		{
			OutCheckpoint = Index;
			return From;
		}
		Elapsed -= FlightHoldSeconds;
		if (Index == StopCount - 1)
		{
			break;
		}
		const double To = FVoxelLandClipmapFlightRoute::StopsCm[Index + 1];
		const double TravelSeconds = FMath::Abs(To - From) / FVoxelLandClipmapFlightRoute::CruiseSpeedCmPerSecond;
		if (Elapsed < TravelSeconds)
		{
			return FMath::Lerp(From, To, Elapsed / TravelSeconds);
		}
		Elapsed -= TravelSeconds;
	}
	bOutComplete = true;
	return FVoxelLandClipmapFlightRoute::StopsCm[StopCount - 1];
}

void AVoxelLandClipmapDemo::AdvanceFlight()
{
	const double Now = FPlatformTime::Seconds();
	int32 Checkpoint = INDEX_NONE;
	bool bComplete = false;
	const double Offset = GetFlightOffsetCm(Now - FlightStartedAt, Checkpoint, bComplete);
	FVector LocalView;
	if (VoxelLandWorldCoordinates::ToLocal(
			FlightOrigin + FVector(Offset, 0.0, 0.0), GetWorld()->OriginLocation, LocalView))
	{
		FlightCamera->SetActorLocation(LocalView);
	}
	if (Checkpoint != FlightLastCheckpoint && Checkpoint != INDEX_NONE)
	{
		TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight checkpoint %d"), Checkpoint);
		UE_LOG(LogTemp, Display, TEXT("ClipmapFlight checkpoint=%d offsetCm=%.0f"), Checkpoint, Offset);
	}
	FlightLastCheckpoint = Checkpoint;

	if (Now >= NextFlightSampleAt || bComplete)
	{
		NextFlightSampleAt = Now + FVoxelLandClipmapFlightRoute::SampleIntervalSeconds;
		FString Coverage;
		if (DescribeClipmapFlightCoverage(FlightProbes, FlightLastTicket, Coverage))
		{
			TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight revision %llu"), FlightLastTicket);
			UE_LOG(LogTemp, Display, TEXT("ClipmapFlight revision=%llu checkpoint=%d offsetCm=%.0f %s"),
				FlightLastTicket, Checkpoint, Offset, *Coverage);
		}
	}
	if (bComplete)
	{
		TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight complete"));
		UE_LOG(LogTemp, Display, TEXT("ClipmapFlight complete lastRevision=%llu error=%s"), FlightLastTicket,
			*M7ClipmapError);
		StopFlight();
	}
}

void AVoxelLandClipmapDemo::StopFlight()
{
	bFlightReady = false;
	bFlightRequested = false;
	ACameraActor* Camera = FlightCamera.Get();
	UWorld* World = GetWorld();
	if (Camera && World && !World->bIsTearingDown)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		if (Controller && Controller->GetViewTarget() == Camera)
		{
			Controller->SetViewTarget(FlightPreviousView.IsValid() ? FlightPreviousView.Get() : Controller);
		}
		Camera->Destroy();
	}
	FlightCamera.Reset();
	FlightPreviousView.Reset();
}

#endif
