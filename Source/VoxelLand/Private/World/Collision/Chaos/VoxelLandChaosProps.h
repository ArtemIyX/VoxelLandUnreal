#pragma once

#include "VoxelLandChaosManagedProp.h"

class AVoxelLandWorld;
class UPrimitiveComponent;
struct FVoxelLandCollisionWorldView;
struct FHitResult;

class FVoxelLandChaosProps final
{
public:
	[[nodiscard]] bool Register(
		AVoxelLandWorld& Terrain, UPrimitiveComponent& Component, FGuid& OutId, bool bSimulateWhenReady = true);
	void Unregister(AVoxelLandWorld& Terrain, const FGuid& ConsumerId);
	void Tick(AVoxelLandWorld& Terrain);
	void SuspendAll();
	void Reset();
	bool HasManagedProps() const { return !Props.IsEmpty(); }
	int64 GetHolds() const { return Holds; }
	int64 GetRecoveries() const { return Recoveries; }

private:
	static constexpr int32 MaxManagedProps = 16;
	static constexpr double PredictionSeconds = 0.2;
	static constexpr double MinimumMarginCm = 4.0;
	static constexpr double SpawnProbeCm = 800.0;
	static constexpr double RecoveryLimitCm = 150.0;
	static constexpr double TeleportThresholdCm = 250.0;
	static constexpr double PenetrationThresholdVoxels = 0.25;

	void UpdateProp(AVoxelLandWorld& Terrain, const FGuid& Id, FVoxelLandChaosManagedProp& Prop);
	void Suspend(FVoxelLandChaosManagedProp& Prop);
	static void Resume(FVoxelLandChaosManagedProp& Prop);
	static FBox BuildFootprint(const FVoxelLandChaosManagedProp& Prop, UPrimitiveComponent& Component, double GravityZ);
	static bool IsSupportHit(const FHitResult& Hit);
	static bool HasTerrainSupport(UPrimitiveComponent& Component);
	static bool IsPoseClear(UPrimitiveComponent& Component, const FTransform& Pose);
	static bool Recover(FVoxelLandChaosManagedProp& Prop, const FVoxelLandCollisionWorldView& View);
	static void RememberSafePose(FVoxelLandChaosManagedProp& Prop, const FVoxelLandCollisionWorldView& View);

	TMap<FGuid, FVoxelLandChaosManagedProp> Props;
	int64 Holds = 0;
	int64 Recoveries = 0;
};
