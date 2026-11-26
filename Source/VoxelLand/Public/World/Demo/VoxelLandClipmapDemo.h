#pragma once

#include "Engine/TimerHandle.h"
#include "World/VoxelLandWorld.h"

#include "VoxelLandClipmapDemo.generated.h"

class ACameraActor;
class UVoxelLandWorldConfig;

UCLASS(Placeable, Blueprintable, meta = (DisplayName = "VoxelLand Clipmap Demo"))
class VOXELLAND_API AVoxelLandClipmapDemo : public AVoxelLandWorld
{
	GENERATED_BODY()

public:
	AVoxelLandClipmapDemo();
#if !UE_BUILD_SHIPPING && !UE_SERVER
	virtual void Tick(float DeltaSeconds) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Demo",
		meta = (DisplayName = "World Config",
			ToolTip =
				"Assign a VoxelLand World Config with Height Terrain and Infinite selected. Streams around the player camera in Play mode."))
	TObjectPtr<UVoxelLandWorldConfig> TerrainConfig;

	FTimerHandle StartupTimer;

	void StartTerrain();

	UFUNCTION()
	void HandleInitializationFinished(bool bSuccess);

#if !UE_BUILD_SHIPPING && !UE_SERVER
	void ConfigureFlight();
	void StartFlight();
	void AdvanceFlight();
	void SampleFlightCoverage(int32 Checkpoint, double Offset);
	void StopFlight();
	double GetSettledFlightElapsed(double Now);
	double GetFlightOffsetCm(double Elapsed, int32& OutCheckpoint, bool& bOutComplete) const;

	TWeakObjectPtr<ACameraActor> FlightCamera;
	TWeakObjectPtr<AActor> FlightPreviousView;
	FVector FlightOrigin = FVector::ZeroVector;
	TArray<FVector2D> FlightProbes;
	double FlightStartedAt = 0.0;
	double NextFlightSampleAt = 0.0;
	double FlightHoldSeconds = 5.0;
	uint64 FlightLastTicket = 0;
	uint64 FlightDepartureTicket = 0;
	int32 FlightLastCheckpoint = INDEX_NONE;
	bool bFlightRequested = false;
	bool bFlightReady = false;
	bool bFlightDepartureSettled = false;
	bool bFlightReturnSettled = false;
#endif
};
