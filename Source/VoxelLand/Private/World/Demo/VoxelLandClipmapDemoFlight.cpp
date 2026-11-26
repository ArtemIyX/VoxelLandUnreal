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
	static constexpr double SettleTimeoutSeconds = 30.0;
	static constexpr double ProbeOffsetCm = 137.0;
	static constexpr double CameraPitch = -12.0;
	static constexpr double StopsCm[] = {0.0, 55000.0, 110000.0, 165000.0, 192000.0, 207000.0, 215500.0, 221000.0, 0.0};

	static double GetReturnArrivalSeconds(double HoldSeconds)
	{
		double Arrival = 0.0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(StopsCm) - 1; ++Index)
		{
			Arrival += HoldSeconds + FMath::Abs(StopsCm[Index + 1] - StopsCm[Index]) / CruiseSpeedCmPerSecond;
		}
		return Arrival;
	}
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
	FlightLastTicket = 0;
	FlightDepartureTicket = 0;
	FlightLastCheckpoint = INDEX_NONE;
	bFlightDepartureSettled = false;
	bFlightReturnSettled = false;
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

double AVoxelLandClipmapDemo::GetSettledFlightElapsed(double Now)
{
	const double Elapsed = Now - FlightStartedAt;
	using FRoute = FVoxelLandClipmapFlightRoute;
	if (!bFlightDepartureSettled)
	{
		FlightDepartureTicket = GetSettledClipmapFlightTicket(FlightOrigin);
		if (FlightDepartureTicket != 0 || Elapsed >= FRoute::SettleTimeoutSeconds)
		{
			bFlightDepartureSettled = true;
			FlightStartedAt = Now;
			TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight settled departure revision %llu"), FlightDepartureTicket);
			UE_LOG(LogTemp, Display,
				TEXT("ClipmapFlight settle endpoint=departure revision=%llu waited=%.2fs timeout=%d"),
				FlightDepartureTicket, Elapsed, FlightDepartureTicket == 0 ? 1 : 0);
		}
		return 0.0;
	}

	const double ReturnArrival = FRoute::GetReturnArrivalSeconds(FlightHoldSeconds);
	if (bFlightReturnSettled || Elapsed < ReturnArrival)
	{
		return Elapsed;
	}
	const double Waited = Elapsed - ReturnArrival;
	const uint64 Ticket = GetSettledClipmapFlightTicket(FlightOrigin);
	const bool bHasReturnRevision = Ticket > FlightDepartureTicket;
	if (bHasReturnRevision || Waited >= FRoute::SettleTimeoutSeconds)
	{
		bFlightReturnSettled = true;
		FlightStartedAt += Waited;
		TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight settled return revision %llu"), Ticket);
		UE_LOG(LogTemp, Display, TEXT("ClipmapFlight settle endpoint=return revision=%llu waited=%.2fs timeout=%d"),
			Ticket, Waited, bHasReturnRevision ? 0 : 1);
	}
	return ReturnArrival;
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
		if (Elapsed + UE_DOUBLE_SMALL_NUMBER < TravelSeconds)
		{
			return FMath::Lerp(From, To, Elapsed / TravelSeconds);
		}
		Elapsed = FMath::Max(0.0, Elapsed - TravelSeconds);
	}
	bOutComplete = true;
	return FVoxelLandClipmapFlightRoute::StopsCm[StopCount - 1];
}

void AVoxelLandClipmapDemo::AdvanceFlight()
{
	const double Now = FPlatformTime::Seconds();
	int32 Checkpoint = INDEX_NONE;
	bool bComplete = false;
	const double Offset = GetFlightOffsetCm(GetSettledFlightElapsed(Now), Checkpoint, bComplete);
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
		SampleFlightCoverage(Checkpoint, Offset);
	}
	if (bComplete)
	{
		TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight complete"));
		UE_LOG(LogTemp, Display, TEXT("ClipmapFlight complete lastRevision=%llu error=%s"), FlightLastTicket,
			*M7ClipmapError);
		StopFlight();
	}
}

void AVoxelLandClipmapDemo::SampleFlightCoverage(int32 Checkpoint, double Offset)
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller || Controller->GetViewTarget() != FlightCamera.Get())
	{
		return;
	}
	FVector LocalCamera;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(LocalCamera, CameraRotation);
	FVector LogicalCamera;
	if (!VoxelLandWorldCoordinates::ToGlobal(LocalCamera, World->OriginLocation, LogicalCamera))
	{
		return;
	}

	FString Coverage;
	if (DescribeClipmapFlightCoverage(LogicalCamera, FlightProbes, FlightLastTicket, Coverage))
	{
		TRACE_BOOKMARK(TEXT("VoxelLand clipmap flight revision %llu"), FlightLastTicket);
		UE_LOG(LogTemp, Display, TEXT("ClipmapFlight revision=%llu checkpoint=%d offsetCm=%.0f %s"), FlightLastTicket,
			Checkpoint, Offset, *Coverage);
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
