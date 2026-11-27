#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UPrimitiveComponent;

struct FVoxelLandChaosManagedProp
{
	TWeakObjectPtr<UPrimitiveComponent> Component;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	TOptional<FTransform> SafeLogicalPose;
	TOptional<FVector> PreviousLogicalLocation;
	uint64 SafePoseEpoch = 0;
	bool bSuspended = false;
	bool bWasAwake = true;
	bool bNeedsSupport = true;
	bool bNeedsRecoveryCoverage = false;
	bool bWantsSimulation = true;
	bool bReportedUnrecoverable = false;
};
