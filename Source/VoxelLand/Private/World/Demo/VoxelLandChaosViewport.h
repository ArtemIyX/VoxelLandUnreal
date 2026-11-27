#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && !UE_SERVER

#include "HAL/IConsoleManager.h"

class APlayerController;
class AVoxelLandWorld;
class UCanvas;

class FVoxelLandChaosViewport final
{
public:
	explicit FVoxelLandChaosViewport(AVoxelLandWorld& InTerrain);
	~FVoxelLandChaosViewport();

private:
	void Draw(UCanvas* Canvas, APlayerController* Controller) const;

	static TAutoConsoleVariable<int32> ShowCookWork;

	TWeakObjectPtr<AVoxelLandWorld> Terrain;
	FDelegateHandle DrawHandle;
};

#endif
