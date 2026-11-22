#include "Generation/VoxelLandM5SlopedFloorSupport.h"

#include "Generation/VoxelLandM5FloorSupport.h"

namespace
{
constexpr int64 CoordinateLimit = 1048576;
constexpr int32 CoefficientLimit = 32767;

FVoxelLandM5Diagnostic SlopedFloorError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = FName(TEXT("SlopedFloorSupport"));
	Result.Message = Message;
	return Result;
}

bool InEnvelope(int64 Value)
{
	return Value >= -CoordinateLimit && Value <= CoordinateLimit;
}

bool InEnvelope(const FVoxelLandM5PointCm& Point)
{
	return InEnvelope(Point.X) && InEnvelope(Point.Y) && InEnvelope(Point.Z);
}

bool OnPlane(const FVoxelLandM5PointCm& Point, const FVoxelLandM5SupportPlane& Plane)
{
	// Every difference is <= 2^21 and each coefficient <= 32767; the
	// sum of products is below 2^38. All intermediate operations fit int64.
	const int64 Left = (Point.Z - Plane.Origin.Z) * int64(Plane.Denominator);
	const int64 Right =
		(Point.X - Plane.Origin.X) * int64(Plane.GradientX) + (Point.Y - Plane.Origin.Y) * int64(Plane.GradientY);
	return Left == Right;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::SlopedFloorSupport::CheckCoplanarRectangularSupport(
	const FVoxelLandM5ClosedBoxCm& Footprint, const FVoxelLandM5SupportPlane& Plane, int32 MaxSlopeNumerator,
	int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, int64 MaxChecks,
	TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return SlopedFloorError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Sloped support check cancelled"));
	}
	if (!InEnvelope(Footprint.Min) || !InEnvelope(Footprint.Max) || !InEnvelope(Plane.Origin)
		|| Footprint.Min.X >= Footprint.Max.X || Footprint.Min.Y >= Footprint.Max.Y || Footprint.Min.Z != 0
		|| Footprint.Max.Z != 0 || Plane.Denominator < 1 || Plane.Denominator > CoefficientLimit
		|| Plane.GradientX < -CoefficientLimit || Plane.GradientX > CoefficientLimit
		|| Plane.GradientY < -CoefficientLimit || Plane.GradientY > CoefficientLimit || MaxSlopeNumerator < 0
		|| MaxSlopeNumerator > CoefficientLimit || MaxSlopeDenominator < 1 || MaxSlopeDenominator > CoefficientLimit
		|| MaxChecks < 0)
	{
		return SlopedFloorError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid support plane, footprint or limit"));
	}
	if (Triangles.Num() > 256 || MaxChecks < Triangles.Num())
	{
		return SlopedFloorError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support preprocessing budget exceeded"));
	}
	const int64 GX = Plane.GradientX;
	const int64 GY = Plane.GradientY;
	const int64 Den = Plane.Denominator;
	const int64 SlopeN = MaxSlopeNumerator;
	const int64 SlopeD = MaxSlopeDenominator;
	// Maximum left side is 2*32767^4 < 2^63; right is <= 32767^4.
	if ((GX * GX + GY * GY) * SlopeD * SlopeD > Den * Den * SlopeN * SlopeN)
	{
		return SlopedFloorError(EVoxelLandM5ErrorCode::Unproven, TEXT("Floor slope exceeds the requested bound"));
	}
	TArray<FVoxelLandM5FloorTriangleCm> Projected;
	Projected.Reserve(Triangles.Num());
	for (const FVoxelLandM5FloorTriangleCm& Triangle : Triangles)
	{
		if (IsCancelled())
		{
			return SlopedFloorError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Sloped support check cancelled"));
		}
		if (!InEnvelope(Triangle.A) || !InEnvelope(Triangle.B) || !InEnvelope(Triangle.C) || !OnPlane(Triangle.A, Plane)
			|| !OnPlane(Triangle.B, Plane) || !OnPlane(Triangle.C, Plane))
		{
			return SlopedFloorError(EVoxelLandM5ErrorCode::Unproven, TEXT("Triangle is outside the plane or domain"));
		}
		FVoxelLandM5FloorTriangleCm Projection = Triangle;
		Projection.A.Z = Projection.B.Z = Projection.C.Z = 0;
		Projected.Add(Projection);
	}
	return VoxelLand::M5::FloorSupport::CheckHorizontalRectangularSupport(
		Footprint, Projected, MaxChecks - Triangles.Num(), IsCancelled);
}
