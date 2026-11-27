#pragma once

#include "World/Collision/Chaos/VoxelLandChaosCookResult.h"
#include "Templates/Function.h"

class VOXELLAND_API FVoxelLandChaosCook final
{
public:
	using FCompletion = TUniqueFunction<void(FVoxelLandChaosCookResultPtr)>;

	// Game thread only. False rejects without a callback. True can complete inline.
	// A null result means the cook failed. The caller must not hold a world strongly in Completion.
	[[nodiscard]] static bool Start(
		TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh, FCompletion Completion);

private:
	static void Finish(bool bSuccess, TSharedRef<FVoxelLandChaosCookResult, ESPMode::ThreadSafe> Result,
		TSharedRef<FCompletion, ESPMode::ThreadSafe> Completion);
#if !UE_BUILD_SHIPPING
	static float GetDeliveryDelay();
	static bool DeliverDelayed(float DeltaSeconds, TSharedRef<FVoxelLandChaosCookResult, ESPMode::ThreadSafe> Result,
		TSharedRef<FCompletion, ESPMode::ThreadSafe> Completion);
#endif
};
