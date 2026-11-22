#include "Generation/VoxelLandM5Geometry.h"

#include <cmath>
#include <limits>

namespace VoxelLand::M5::Geometry
{
namespace
{
bool IsFiniteVector(const FVector3d& V)
{
	return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
}

bool IsValidRadius(double Radius)
{
	return std::isfinite(Radius) && Radius > 0.0;
}

bool TryDifference(const FVector3d& A, const FVector3d& B, FVector3d& Out)
{
	const double X = A.X - B.X;
	const double Y = A.Y - B.Y;
	const double Z = A.Z - B.Z;
	if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z))
	{
		return false;
	}
	Out = FVector3d(X, Y, Z);
	return true;
}

bool TryLength(const FVector3d& V, double& Out)
{
	if (!IsFiniteVector(V))
	{
		return false;
	}
	const double Length = std::hypot(V.X, V.Y, V.Z);
	if (!std::isfinite(Length))
	{
		return false;
	}
	Out = Length;
	return true;
}

bool TryBounds(FVector3d Low, FVector3d High, double Radius, FBox3d& Out)
{
	if (!IsFiniteVector(Low) || !IsFiniteVector(High) || !IsValidRadius(Radius))
	{
		return false;
	}
	const double Infinity = std::numeric_limits<double>::infinity();
	// Expand by one representable value to contain the real result even if
	// the subtraction or addition rounded inwards. Reject unbounded boxes.
	const double MinX = std::nextafter(Low.X - Radius, -Infinity);
	const double MinY = std::nextafter(Low.Y - Radius, -Infinity);
	const double MinZ = std::nextafter(Low.Z - Radius, -Infinity);
	const double MaxX = std::nextafter(High.X + Radius, Infinity);
	const double MaxY = std::nextafter(High.Y + Radius, Infinity);
	const double MaxZ = std::nextafter(High.Z + Radius, Infinity);
	if (!std::isfinite(MinX) || !std::isfinite(MinY) || !std::isfinite(MinZ) || !std::isfinite(MaxX)
		|| !std::isfinite(MaxY) || !std::isfinite(MaxZ))
	{
		return false;
	}
	Out = FBox3d(FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MaxY, MaxZ));
	return true;
}
}

bool TrySphereDistance(FVector3d P, FVector3d Center, double Radius, double& Out)
{
	if (!IsFiniteVector(P) || !IsFiniteVector(Center) || !IsValidRadius(Radius))
	{
		return false;
	}
	FVector3d Offset(0, 0, 0);
	double Length = 0.0;
	if (!TryDifference(P, Center, Offset) || !TryLength(Offset, Length))
	{
		return false;
	}
	const double Distance = Length - Radius;
	if (!std::isfinite(Distance))
	{
		return false;
	}
	Out = Distance;
	return true;
}

bool TrySegmentDistance(FVector3d P, FVector3d A, FVector3d B, double Radius, double& Out)
{
	if (!IsFiniteVector(P) || !IsFiniteVector(A) || !IsFiniteVector(B) || !IsValidRadius(Radius))
	{
		return false;
	}
	FVector3d Segment(0, 0, 0);
	FVector3d Offset(0, 0, 0);
	double SegmentLength = 0.0;
	if (!TryDifference(B, A, Segment) || !TryDifference(P, A, Offset) || !TryLength(Segment, SegmentLength)
		|| SegmentLength == 0.0)
	{
		return false;
	}
	const double DX = Segment.X / SegmentLength;
	const double DY = Segment.Y / SegmentLength;
	const double DZ = Segment.Z / SegmentLength;
	if (!std::isfinite(DX) || !std::isfinite(DY) || !std::isfinite(DZ))
	{
		return false;
	}
	const FVector3d Direction(DX, DY, DZ);
	const double Projection = FVector3d::DotProduct(Offset, Direction);
	if (!std::isfinite(Projection))
	{
		return false;
	}
	const double Along = FMath::Clamp(Projection, 0.0, SegmentLength);
	const double X = A.X + Direction.X * Along;
	const double Y = A.Y + Direction.Y * Along;
	const double Z = A.Z + Direction.Z * Along;
	if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z))
	{
		return false;
	}
	const FVector3d Closest(X, Y, Z);
	FVector3d Delta(0, 0, 0);
	double Length = 0.0;
	if (!TryDifference(P, Closest, Delta) || !TryLength(Delta, Length))
	{
		return false;
	}
	const double Distance = Length - Radius;
	if (!std::isfinite(Distance))
	{
		return false;
	}
	Out = Distance;
	return true;
}

bool TrySphereBounds(FVector3d Center, double Radius, FBox3d& Out)
{
	return TryBounds(Center, Center, Radius, Out);
}

bool TrySegmentBounds(FVector3d A, FVector3d B, double Radius, FBox3d& Out)
{
	if (!IsFiniteVector(A) || !IsFiniteVector(B) || A == B)
	{
		return false;
	}
	const FVector3d Low(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
	const FVector3d High(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
	return TryBounds(Low, High, Radius, Out);
}
}
