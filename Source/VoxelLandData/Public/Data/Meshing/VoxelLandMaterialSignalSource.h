#pragma once

#include "CoreMinimal.h"

class IVoxelLandMaterialSignalSource
{
public:
	virtual ~IVoxelLandMaterialSignalSource() = default;
	virtual bool SampleTerrain(FGuid OutputId, const FVector3d& Position, double& OutValue) const = 0;
	virtual bool SampleWorld(FName Channel, const FVector2d& Position, double& OutValue) const = 0;
};
