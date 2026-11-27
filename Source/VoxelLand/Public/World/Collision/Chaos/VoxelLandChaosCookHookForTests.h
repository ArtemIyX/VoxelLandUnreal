#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "World/Collision/Chaos/VoxelLandChaosCook.h"

using FVoxelLandChaosCookHookForTests =
	TFunction<bool(TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe>, FVoxelLandChaosCook::FCompletion)>;
#endif
