#include "Generation/VoxelLandM5RouteCertificate.h"

#include "Generation/VoxelLandM5CheckedMath.h"

namespace
{
FVoxelLandM5Diagnostic RouteBoxError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = FName(TEXT("RouteBoxSeparation"));
	Result.Message = Message;
	return Result;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::RouteCertificate::CheckSweptCapsuleBoxSeparation(
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, TConstArrayView<FVoxelLandM5ClosedBoxCm> Obstacles, int64 MaxChecks,
	TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Route box check cancelled"));
	}
	if (RadiusCm <= 0 || HeightCm <= 0 || MarginCm < 0 || MaxChecks < 0)
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid capsule dimensions or budget"));
	}
	int64 Diameter = 0;
	int64 HorizontalExtent = 0;
	if (!TryMultiply(RadiusCm, 2, Diameter) || !TryAdd(RadiusCm, MarginCm, HorizontalExtent))
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Capsule extent overflow"));
	}
	if (HeightCm < Diameter)
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Capsule height is less than its diameter"));
	}
	if (Obstacles.Num() > MaxChecks)
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Route box work budget exceeded"));
	}
	FVoxelLandM5ClosedBoxCm Sweep;
	if (!TrySubtract(FMath::Min(StartFeet.X, EndFeet.X), HorizontalExtent, Sweep.Min.X)
		|| !TrySubtract(FMath::Min(StartFeet.Y, EndFeet.Y), HorizontalExtent, Sweep.Min.Y)
		|| !TrySubtract(FMath::Min(StartFeet.Z, EndFeet.Z), MarginCm, Sweep.Min.Z)
		|| !TryAdd(FMath::Max(StartFeet.X, EndFeet.X), HorizontalExtent, Sweep.Max.X)
		|| !TryAdd(FMath::Max(StartFeet.Y, EndFeet.Y), HorizontalExtent, Sweep.Max.Y)
		|| !TryAdd(FMath::Max(StartFeet.Z, EndFeet.Z), HeightCm, Sweep.Max.Z)
		|| !TryAdd(Sweep.Max.Z, MarginCm, Sweep.Max.Z))
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Swept capsule bounds overflow"));
	}
	bool bOverlap = false;
	for (int32 Index = 0; Index < Obstacles.Num(); ++Index)
	{
		if ((Index & 63) == 0 && IsCancelled())
		{
			return RouteBoxError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Route box check cancelled"));
		}
		const FVoxelLandM5ClosedBoxCm& Box = Obstacles[Index];
		if (Box.Min.X > Box.Max.X || Box.Min.Y > Box.Max.Y || Box.Min.Z > Box.Max.Z)
		{
			return RouteBoxError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Obstacle bounds are not ordered"));
		}
		bOverlap |= Box.Min.X <= Sweep.Max.X && Box.Max.X >= Sweep.Min.X && Box.Min.Y <= Sweep.Max.Y
				 && Box.Max.Y >= Sweep.Min.Y && Box.Min.Z <= Sweep.Max.Z && Box.Max.Z >= Sweep.Min.Z;
	}
	if (IsCancelled())
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Route box check cancelled"));
	}
	if (bOverlap)
	{
		return RouteBoxError(EVoxelLandM5ErrorCode::Unproven, TEXT("Obstacle bounds intersect swept capsule bounds"));
	}
	return FVoxelLandM5Diagnostic{};
}
