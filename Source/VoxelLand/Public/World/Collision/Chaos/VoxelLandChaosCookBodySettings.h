#pragma once

#include "Engine/EngineTypes.h"

struct FVoxelLandChaosCookBodySettings
{
	ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	ECollisionChannel ObjectType = ECC_WorldStatic;
	FCollisionResponseContainer Responses{ECR_Block};
	bool bDrawCollision = false;
};
