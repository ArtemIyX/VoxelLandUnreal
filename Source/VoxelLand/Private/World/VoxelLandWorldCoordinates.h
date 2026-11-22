#pragma once
#include "CoreMinimal.h"

namespace VoxelLandWorldCoordinates
{
// Terrain keys and generator positions use global centimeters; Unreal world
// positions are relative to UWorld::OriginLocation. Leave output untouched for
// nonfinite inputs or results. FIntVector origins convert exactly to double.
inline bool ToGlobal(const FVector& Local, const FIntVector& Origin, FVector& Out)
{
	const FVector Result = Local + FVector(double(Origin.X), double(Origin.Y), double(Origin.Z));
	if (!FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y) || !FMath::IsFinite(Local.Z)
		|| !FMath::IsFinite(Result.X) || !FMath::IsFinite(Result.Y) || !FMath::IsFinite(Result.Z))
	{
		return false;
	}
	Out = Result;
	return true;
}
inline bool ToLocal(const FVector& Global, const FIntVector& Origin, FVector& Out)
{
	const FVector Result = Global - FVector(double(Origin.X), double(Origin.Y), double(Origin.Z));
	if (!FMath::IsFinite(Global.X) || !FMath::IsFinite(Global.Y) || !FMath::IsFinite(Global.Z)
		|| !FMath::IsFinite(Result.X) || !FMath::IsFinite(Result.Y) || !FMath::IsFinite(Result.Z))
	{
		return false;
	}
	Out = Result;
	return true;
}
}
