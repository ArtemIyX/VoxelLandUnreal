#pragma once

#include "Engine/TimerHandle.h"
#include "World/VoxelLandWorld.h"
#include "VoxelLandChaosDemo.generated.h"

class UVoxelLandWorldConfig;

/** Placeable opted-in Chaos terrain playground. The assigned config is never changed. */
UCLASS(Blueprintable, meta = (DisplayName = "VoxelLand Chaos Playground"))
class VOXELLAND_API AVoxelLandChaosDemo : public AVoxelLandWorld
{
	GENERATED_BODY()

public:
	AVoxelLandChaosDemo();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Playground")
	TObjectPtr<UVoxelLandWorldConfig> TerrainConfig;

	// Enable the optional backend only for this playground's runtime instance.
	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Playground")
	bool bEnableChaosForPlayground = false;

	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Playground", meta = (ClampMin = "0", ClampMax = "8"))
	int32 InitialPropCount = 4;

	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Playground", meta = (Units = "cm"))
	FVector FirstDropOffset = FVector(1600.0, 1600.0, 2500.0);

	UPROPERTY(
		EditInstanceOnly, Category = "VoxelLand|Playground", meta = (ClampMin = "5", ClampMax = "100", Units = "cm"))
	float PropRadiusCm = 35.0f;

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Playground")
	AActor* DropPlaygroundProp(FVector WorldPosition);

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Playground")
	bool SetPlaygroundCollisionEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Playground")
	void ClearPlaygroundProps();

private:
	void StartTerrain();
#if !UE_BUILD_SHIPPING
	void StartSmoke();
	void CheckSmoke();
	void FinishSmoke(bool bPassed);
	void CaptureSmokeViews();
	FTimerHandle SmokeTimer;
	double SmokeStartTime = 0.0;
	int32 SmokeCaptureStep = 0;
	FString SmokeCollisionImage;
	FString SmokeLitImage;
	bool bSmokeRequested = false;
	bool bCaptureSmokeViews = false;
#endif

	UFUNCTION()
	void HandleTerrainInitialization(bool bSuccess);

	static constexpr int32 MaxPlaygroundProps = 16;
	static constexpr double SpawnSpacingCm = 150.0;
	FTimerHandle StartupTimer;
	TMap<FGuid, TWeakObjectPtr<AActor>> Props;
	bool bTerrainReady = false;
	bool bCollisionEnabled = false;
};
