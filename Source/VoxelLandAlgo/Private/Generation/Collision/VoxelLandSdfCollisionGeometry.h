#pragma once

#include "CoreMinimal.h"

class FVoxelLandSdfCollisionGeometry final
{
public:
	static constexpr double RoundingPad = 1.0e-7;

	FVector3d Start = FVector3d::ZeroVector;
	FVector3d Delta = FVector3d::ZeroVector;
	double Radius = 0.0;
	double Skin = 0.0;
	double SpineHalfLength = 0.0;

	// The lower endpoint is before the first contact. The upper endpoint is
	// inside the closed shape, within the geometry time refinement tolerance.
	[[nodiscard]] bool Intersect(const FBox3d& Box, double ExtraRadius, double& OutLower, double& OutUpper) const;
	[[nodiscard]] bool CanStartPenetrating(const FBox3d& Box) const;
	[[nodiscard]] FVector3d GetNearestBoxPoint(const FBox3d& Box, double Time) const;
	[[nodiscard]] FVector3d GetNearestSpinePoint(const FVector3d& Point, double Time) const;
	[[nodiscard]] double GetInteriorMargin(const FVector3d& Point) const;

private:
	static constexpr int32 TimeRefinementSteps = 36;
	[[nodiscard]] double FindClosestTime(const FBox3d& Box) const;
	[[nodiscard]] static FVector3d OffsetFromBox(const FVector3d& Point, const FBox3d& Box);
};
