#include "Generation/Caves/Geometry/VoxelLandCaveTriangleSupport.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

class FVoxelLandM5TriangleSupportInternals final
{
public:
	static constexpr int64 M5TriangleSupportLimit = 1048576;
	static constexpr int32 M5TriangleSupportCoefficientLimit = 32767;
	static FVoxelLandM5Diagnostic M5SupportError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static bool InRange(int64 V);
	static bool InRange(const FVoxelLandM5PointCm& P);
	static bool EqualXY(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B);
	static int64 Orient(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& C);
	static const FVoxelLandM5PointCm& Vertex(const FVoxelLandM5FloorTriangleCm& T, int32 Index);
	static bool OnSegment(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& P);
	static bool StrictlyInside(const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P);
	static bool ContainsMidpoint(const FVoxelLandM5FloorTriangleCm& T, int64 TwiceX, int64 TwiceY);
	static bool InsideOrBoundary(const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P);
	static bool EdgePairCompatible(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B,
		const FVoxelLandM5PointCm& C, const FVoxelLandM5PointCm& D);
	static bool M5TriangleSupportOnPlane(const FVoxelLandM5PointCm& P, const FVoxelLandM5SupportPlane& Plane);
	static bool SquareProduct(int64 A, int64 B, int64& Out);
};

FVoxelLandM5Diagnostic FVoxelLandM5TriangleSupportInternals::M5SupportError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.TriangleSupport");
	Result.Message = Message;
	return Result;
}

bool FVoxelLandM5TriangleSupportInternals::InRange(int64 V)
{
	return V >= -M5TriangleSupportLimit && V <= M5TriangleSupportLimit;
}
bool FVoxelLandM5TriangleSupportInternals::InRange(const FVoxelLandM5PointCm& P)
{
	return InRange(P.X) && InRange(P.Y) && InRange(P.Z);
}
bool FVoxelLandM5TriangleSupportInternals::EqualXY(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
{
	return A.X == B.X && A.Y == B.Y;
}

int64 FVoxelLandM5TriangleSupportInternals::Orient(
	const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& C)
{
	// All input coordinates are bounded by 2^20, so this is below 2^43.
	return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
}

const FVoxelLandM5PointCm& FVoxelLandM5TriangleSupportInternals::Vertex(
	const FVoxelLandM5FloorTriangleCm& T, int32 Index)
{
	return Index == 0 ? T.A : (Index == 1 ? T.B : T.C);
}

bool FVoxelLandM5TriangleSupportInternals::OnSegment(
	const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& P)
{
	return Orient(A, B, P) == 0 && P.X >= FMath::Min(A.X, B.X) && P.X <= FMath::Max(A.X, B.X)
		&& P.Y >= FMath::Min(A.Y, B.Y) && P.Y <= FMath::Max(A.Y, B.Y);
}

bool FVoxelLandM5TriangleSupportInternals::StrictlyInside(
	const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P)
{
	return Orient(T.A, T.B, P) > 0 && Orient(T.B, T.C, P) > 0 && Orient(T.C, T.A, P) > 0;
}

bool FVoxelLandM5TriangleSupportInternals::ContainsMidpoint(
	const FVoxelLandM5FloorTriangleCm& T, int64 TwiceX, int64 TwiceY)
{
	// Double the point-to-edge displacement to preserve exact half-unit coordinates.
	const auto Side = [TwiceX, TwiceY](const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B) {
		return (B.X - A.X) * (TwiceY - 2 * A.Y) - (B.Y - A.Y) * (TwiceX - 2 * A.X);
	};
	return Side(T.A, T.B) >= 0 && Side(T.B, T.C) >= 0 && Side(T.C, T.A) >= 0;
}

bool FVoxelLandM5TriangleSupportInternals::InsideOrBoundary(
	const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P)
{
	return Orient(T.A, T.B, P) >= 0 && Orient(T.B, T.C, P) >= 0 && Orient(T.C, T.A, P) >= 0;
}

bool FVoxelLandM5TriangleSupportInternals::EdgePairCompatible(const FVoxelLandM5PointCm& A,
	const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& C, const FVoxelLandM5PointCm& D)
{
	// A fully shared edge must have opposite orientation. Partial collinear
	// edges and T-junctions are rejected, including a vertex in an edge interior.
	if (EqualXY(A, D) && EqualXY(B, C))
	{
		return true;
	}
	if (EqualXY(A, C) && EqualXY(B, D))
	{
		return false;
	}
	const int64 O1 = Orient(A, B, C), O2 = Orient(A, B, D);
	const int64 O3 = Orient(C, D, A), O4 = Orient(C, D, B);
	if (((O1 > 0 && O2 < 0) || (O1 < 0 && O2 > 0)) && ((O3 > 0 && O4 < 0) || (O3 < 0 && O4 > 0)))
	{
		return false;
	}
	if (OnSegment(A, B, C) && !EqualXY(C, A) && !EqualXY(C, B))
	{
		return false;
	}
	if (OnSegment(A, B, D) && !EqualXY(D, A) && !EqualXY(D, B))
	{
		return false;
	}
	if (OnSegment(C, D, A) && !EqualXY(A, C) && !EqualXY(A, D))
	{
		return false;
	}
	if (OnSegment(C, D, B) && !EqualXY(B, C) && !EqualXY(B, D))
	{
		return false;
	}
	return true;
}

bool FVoxelLandM5TriangleSupportInternals::M5TriangleSupportOnPlane(
	const FVoxelLandM5PointCm& P, const FVoxelLandM5SupportPlane& Plane)
{
	return (P.Z - Plane.Origin.Z) * int64(Plane.Denominator)
		== (P.X - Plane.Origin.X) * int64(Plane.GradientX) + (P.Y - Plane.Origin.Y) * int64(Plane.GradientY);
}

bool FVoxelLandM5TriangleSupportInternals::SquareProduct(int64 A, int64 B, int64& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	int64 Square = 0;
	return A >= 0 && B >= 0 && TryMultiplyNonNegative(A, A, Square) && TryMultiplyNonNegative(Square, B, Out);
}

FVoxelLandM5Diagnostic VoxelLand::M5::TriangleSupport::CertifyLinearContact(const FVoxelLandM5PointCm& FeetStart,
	const FVoxelLandM5PointCm& FeetEnd, int64 RadiusCm, int64 HeightCm, int64 ContactSkinCm,
	const FVoxelLandM5SupportPlane& Plane, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
	}
	if (!FVoxelLandM5TriangleSupportInternals::InRange(FeetStart)
		|| !FVoxelLandM5TriangleSupportInternals::InRange(FeetEnd)
		|| !FVoxelLandM5TriangleSupportInternals::InRange(Plane.Origin) || RadiusCm < 1
		|| RadiusCm > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportLimit || HeightCm < 2 * RadiusCm
		|| HeightCm > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportLimit || ContactSkinCm < 0
		|| ContactSkinCm > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportLimit
		|| RadiusCm + ContactSkinCm > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportLimit
		|| Plane.Denominator < 1
		|| Plane.Denominator > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| Plane.GradientX < -FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| Plane.GradientX > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| Plane.GradientY < -FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| Plane.GradientY > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| MaxSlopeNumerator < 0
		|| MaxSlopeNumerator > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit
		|| MaxSlopeDenominator < 1
		|| MaxSlopeDenominator > FVoxelLandM5TriangleSupportInternals::M5TriangleSupportCoefficientLimit)
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid capsule, plane or slope"));
	}
	if (Triangles.IsEmpty())
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("No support triangles"));
	}
	if (Triangles.Num() > 256)
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Too many support triangles"));
	}
	const int64 Count = Triangles.Num();
	// No scratch allocation. The fixed charge bounds all triangle/edge pair
	// comparisons and cancellation calls, including exact boundary scans.
	if (!Budget->TryChargeWork(128 + 256 * Count * Count))
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support work budget exhausted"));
	}
	const int64 GX = Plane.GradientX, GY = Plane.GradientY, Den = Plane.Denominator;
	const int64 SN = MaxSlopeNumerator, SD = MaxSlopeDenominator;
	if ((GX * GX + GY * GY) * SD * SD > Den * Den * SN * SN)
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Support slope exceeds limit"));
	}
	const int64 NormalSquared = GX * GX + GY * GY + Den * Den;
	int64 MinSquared = 0, MaxSquared = 0;
	if (!FVoxelLandM5TriangleSupportInternals::SquareProduct(RadiusCm, NormalSquared, MinSquared)
		|| !FVoxelLandM5TriangleSupportInternals::SquareProduct(RadiusCm + ContactSkinCm, NormalSquared, MaxSquared))
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Contact predicate exceeds exact arithmetic range"));
	}
	for (const FVoxelLandM5PointCm* Feet : {&FeetStart, &FeetEnd})
	{
		int64 TopZ = 0;
		if (!TryAdd(Feet->Z, HeightCm, TopZ) || !FVoxelLandM5TriangleSupportInternals::InRange(TopZ))
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Capsule exceeds domain"));
		}
		const int64 CenterZ = Feet->Z + RadiusCm;
		const int64 Dot =
			-(Feet->X - Plane.Origin.X) * GX - (Feet->Y - Plane.Origin.Y) * GY + (CenterZ - Plane.Origin.Z) * Den;
		int64 DotSquared = 0;
		if (Dot < 0 || !TryMultiplyNonNegative(Dot, Dot, DotSquared))
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Capsule contact cannot be proven"));
		}
		if (DotSquared < MinSquared || DotSquared > MaxSquared)
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Capsule penetrates or loses contact"));
		}
	}
	const int64 Extent = RadiusCm + ContactSkinCm;
	const int64 MinX = FMath::Min(FeetStart.X, FeetEnd.X) - Extent;
	const int64 MaxX = FMath::Max(FeetStart.X, FeetEnd.X) + Extent;
	const int64 MinY = FMath::Min(FeetStart.Y, FeetEnd.Y) - Extent;
	const int64 MaxY = FMath::Max(FeetStart.Y, FeetEnd.Y) + Extent;
	if (!FVoxelLandM5TriangleSupportInternals::InRange(MinX) || !FVoxelLandM5TriangleSupportInternals::InRange(MaxX)
		|| !FVoxelLandM5TriangleSupportInternals::InRange(MinY) || !FVoxelLandM5TriangleSupportInternals::InRange(MaxY))
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Contact footprint exceeds domain"));
	}
	for (const FVoxelLandM5FloorTriangleCm& T : Triangles)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
		}
		if (!FVoxelLandM5TriangleSupportInternals::InRange(T.A) || !FVoxelLandM5TriangleSupportInternals::InRange(T.B)
			|| !FVoxelLandM5TriangleSupportInternals::InRange(T.C)
			|| !FVoxelLandM5TriangleSupportInternals::M5TriangleSupportOnPlane(T.A, Plane)
			|| !FVoxelLandM5TriangleSupportInternals::M5TriangleSupportOnPlane(T.B, Plane)
			|| !FVoxelLandM5TriangleSupportInternals::M5TriangleSupportOnPlane(T.C, Plane)
			|| FVoxelLandM5TriangleSupportInternals::Orient(T.A, T.B, T.C) <= 0)
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Invalid, noncoplanar or downward triangle"));
		}
	}
	for (int32 I = 0; I < Triangles.Num(); ++I)
	{
		for (int32 J = I + 1; J < Triangles.Num(); ++J)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5TriangleSupportInternals::M5SupportError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
			}
			const auto& A = Triangles[I];
			const auto& B = Triangles[J];
			for (int32 K = 0; K < 3; ++K)
			{
				if (FVoxelLandM5TriangleSupportInternals::StrictlyInside(
						A, FVoxelLandM5TriangleSupportInternals::Vertex(B, K))
					|| FVoxelLandM5TriangleSupportInternals::StrictlyInside(
						B, FVoxelLandM5TriangleSupportInternals::Vertex(A, K)))
				{
					return FVoxelLandM5TriangleSupportInternals::M5SupportError(
						EVoxelLandM5ErrorCode::Unproven, TEXT("Overlapping floor triangles"));
				}
				for (int32 L = 0; L < 3; ++L)
				{
					if (!FVoxelLandM5TriangleSupportInternals::EdgePairCompatible(
							FVoxelLandM5TriangleSupportInternals::Vertex(A, K),
							FVoxelLandM5TriangleSupportInternals::Vertex(A, (K + 1) % 3),
							FVoxelLandM5TriangleSupportInternals::Vertex(B, L),
							FVoxelLandM5TriangleSupportInternals::Vertex(B, (L + 1) % 3)))
					{
						return FVoxelLandM5TriangleSupportInternals::M5SupportError(
							EVoxelLandM5ErrorCode::Unproven, TEXT("Intersecting or nonmanifold floor edges"));
					}
				}
			}
		}
	}
	FVoxelLandM5PointCm Corners[4];
	Corners[0].X = MinX;
	Corners[0].Y = MinY;
	Corners[1].X = MinX;
	Corners[1].Y = MaxY;
	Corners[2].X = MaxX;
	Corners[2].Y = MinY;
	Corners[3].X = MaxX;
	Corners[3].Y = MaxY;
	for (const auto& Corner : Corners)
	{
		bool bCovered = false;
		for (const auto& T : Triangles)
		{
			bCovered |= FVoxelLandM5TriangleSupportInternals::InsideOrBoundary(T, Corner);
		}
		if (!bCovered)
		{
			return FVoxelLandM5TriangleSupportInternals::M5SupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Contact footprint is not covered"));
		}
	}
	// The center is strictly inside the footprint. If it touches any nondegenerate
	// triangle, that triangle has interior points arbitrarily close to the center.
	// The boundary scan below then proves coverage of the connected footprint interior.
	bool bCenterCovered = false;
	const int64 TwiceCenterX = MinX + MaxX;
	const int64 TwiceCenterY = MinY + MaxY;
	for (const FVoxelLandM5FloorTriangleCm& T : Triangles)
	{
		bCenterCovered |= FVoxelLandM5TriangleSupportInternals::ContainsMidpoint(T, TwiceCenterX, TwiceCenterY);
	}
	if (!bCenterCovered)
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Contact footprint center is not covered"));
	}
	for (int32 I = 0; I < Triangles.Num(); ++I)
	{
		for (int32 E = 0; E < 3; ++E)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5TriangleSupportInternals::M5SupportError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
			}
			const auto& A = FVoxelLandM5TriangleSupportInternals::Vertex(Triangles[I], E);
			const auto& B = FVoxelLandM5TriangleSupportInternals::Vertex(Triangles[I], (E + 1) % 3);
			int32 ReverseCount = 0;
			for (int32 J = 0; J < Triangles.Num(); ++J)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5TriangleSupportInternals::M5SupportError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
				}
				if (I == J)
				{
					continue;
				}
				for (int32 F = 0; F < 3; ++F)
				{
					if (FVoxelLandM5TriangleSupportInternals::EqualXY(
							A, FVoxelLandM5TriangleSupportInternals::Vertex(Triangles[J], (F + 1) % 3))
						&& FVoxelLandM5TriangleSupportInternals::EqualXY(
							B, FVoxelLandM5TriangleSupportInternals::Vertex(Triangles[J], F)))
					{
						++ReverseCount;
					}
				}
			}
			if (ReverseCount > 1)
			{
				return FVoxelLandM5TriangleSupportInternals::M5SupportError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Nonmanifold shared edge"));
			}
			if (ReverseCount == 0 && FMath::Min(A.X, B.X) < MaxX && FMath::Max(A.X, B.X) > MinX
				&& FMath::Min(A.Y, B.Y) < MaxY && FMath::Max(A.Y, B.Y) > MinY)
			{
				return FVoxelLandM5TriangleSupportInternals::M5SupportError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Floor boundary may cross the contact footprint"));
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5TriangleSupportInternals::M5SupportError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
	}
	return {};
}
