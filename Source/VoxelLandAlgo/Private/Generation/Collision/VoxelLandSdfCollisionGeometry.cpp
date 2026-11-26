#include "VoxelLandSdfCollisionGeometry.h"

FVector3d FVoxelLandSdfCollisionGeometry::OffsetFromBox(const FVector3d& Point, const FBox3d& Box)
{
	return Point
		 - FVector3d(FMath::Clamp(Point.X, Box.Min.X, Box.Max.X), FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y),
			 FMath::Clamp(Point.Z, Box.Min.Z, Box.Max.Z));
}

double FVoxelLandSdfCollisionGeometry::FindClosestTime(const FBox3d& Box) const
{
	TArray<double, TInlineAllocator<8>> CutTimes;
	CutTimes.Add(0.0);
	CutTimes.Add(1.0);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Delta[Axis] == 0.0)
		{
			continue;
		}
		for (const double Boundary : {Box.Min[Axis], Box.Max[Axis]})
		{
			const double Time = (Boundary - Start[Axis]) / Delta[Axis];
			if (0.0 < Time && Time < 1.0)
			{
				CutTimes.Add(Time);
			}
		}
	}
	CutTimes.Sort();

	// Distance squared to an AABB is a convex quadratic between plane
	// crossings. Only the axes outside the box contribute its derivative.
	double ClosestTime = 0.0;
	double ClosestDistanceSquared = OffsetFromBox(Start, Box).SquaredLength();
	for (int32 Index = 1; Index < CutTimes.Num(); ++Index)
	{
		const double First = CutTimes[Index - 1];
		const double Last = CutTimes[Index];
		const double Middle = (First + Last) * 0.5;
		const FVector3d Offset = OffsetFromBox(Start + Delta * Middle, Box);
		double Curvature = 0.0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Offset[Axis] != 0.0)
			{
				Curvature += FMath::Square(Delta[Axis]);
			}
		}
		const double Time = Curvature > 0.0
							  ? FMath::Clamp(Middle - FVector3d::DotProduct(Offset, Delta) / Curvature, First, Last)
							  : First;
		const double DistanceSquared = OffsetFromBox(Start + Delta * Time, Box).SquaredLength();
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestTime = Time;
		}
	}
	return ClosestTime;
}

bool FVoxelLandSdfCollisionGeometry::Intersect(
	const FBox3d& Box, double ExtraRadius, double& OutLower, double& OutUpper) const
{
	// Minkowski subtraction of the upright spine extends just the Z interval.
	// The remaining test is a sphere moving against this exact extended box.
	const FVector3d Spine(0.0, 0.0, SpineHalfLength);
	const FBox3d SpineBox(Box.Min - Spine, Box.Max + Spine);
	const double RadiusSquared = FMath::Square(Radius + ExtraRadius);
	if (OffsetFromBox(Start, SpineBox).SquaredLength() <= RadiusSquared)
	{
		OutLower = 0.0;
		OutUpper = 0.0;
		return true;
	}
	if (Delta.IsZero())
	{
		return false;
	}

	const double ClosestTime = FindClosestTime(SpineBox);
	if (OffsetFromBox(Start + Delta * ClosestTime, SpineBox).SquaredLength() > RadiusSquared)
	{
		return false;
	}

	double Lower = 0.0;
	double Upper = ClosestTime;
	for (int32 Index = 0; Index < TimeRefinementSteps; ++Index)
	{
		const double Middle = (Lower + Upper) * 0.5;
		if (OffsetFromBox(Start + Delta * Middle, SpineBox).SquaredLength() <= RadiusSquared)
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	OutLower = Lower;
	OutUpper = Upper;
	return true;
}

bool FVoxelLandSdfCollisionGeometry::CanStartPenetrating(const FBox3d& Box) const
{
	const FVector3d Nearest = GetNearestBoxPoint(Box, 0.0);
	return GetInteriorMargin(Nearest) > RoundingPad;
}

FVector3d FVoxelLandSdfCollisionGeometry::GetNearestBoxPoint(const FBox3d& Box, double Time) const
{
	const FVector3d Center = Start + Delta * Time;
	return FVector3d(FMath::Clamp(Center.X, Box.Min.X, Box.Max.X), FMath::Clamp(Center.Y, Box.Min.Y, Box.Max.Y),
		FMath::Clamp(Center.Z, Box.Min.Z, Box.Max.Z));
}

FVector3d FVoxelLandSdfCollisionGeometry::GetNearestSpinePoint(const FVector3d& Point, double Time) const
{
	const FVector3d Center = Start + Delta * Time;
	return FVector3d(Center.X, Center.Y, FMath::Clamp(Point.Z, Center.Z - SpineHalfLength, Center.Z + SpineHalfLength));
}

double FVoxelLandSdfCollisionGeometry::GetInteriorMargin(const FVector3d& Point) const
{
	return Radius - FVector3d::Distance(Point, GetNearestSpinePoint(Point, 0.0));
}
