#include "Generation/VoxelLandM5TriangleSupport.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CheckedMath.h"

namespace
{
constexpr int64 Limit = 1048576;
constexpr int32 CoefficientLimit = 32767;

FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.TriangleSupport");
	Result.Message = Message;
	return Result;
}

bool InRange(int64 V)
{
	return V >= -Limit && V <= Limit;
}
bool InRange(const FVoxelLandM5PointCm& P)
{
	return InRange(P.X) && InRange(P.Y) && InRange(P.Z);
}
bool EqualXY(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
{
	return A.X == B.X && A.Y == B.Y;
}

int64 Orient(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& C)
{
	// All input coordinates are bounded by 2^20, so this is below 2^43.
	return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
}

const FVoxelLandM5PointCm& Vertex(const FVoxelLandM5FloorTriangleCm& T, int32 Index)
{
	return Index == 0 ? T.A : (Index == 1 ? T.B : T.C);
}

bool OnSegment(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& P)
{
	return Orient(A, B, P) == 0 && P.X >= FMath::Min(A.X, B.X) && P.X <= FMath::Max(A.X, B.X)
		&& P.Y >= FMath::Min(A.Y, B.Y) && P.Y <= FMath::Max(A.Y, B.Y);
}

bool StrictlyInside(const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P)
{
	return Orient(T.A, T.B, P) > 0 && Orient(T.B, T.C, P) > 0 && Orient(T.C, T.A, P) > 0;
}

// X4 and Y4 encode a point in quarter-centimetre units. Scaling all
// coordinates by four preserves orientation exactly; bounded inputs keep
// each product below 2^47. No rounding or floating-point predicate is used.
bool StrictlyInsideQuarter(const FVoxelLandM5FloorTriangleCm& T, int64 X4, int64 Y4)
{
	const auto Side = [X4, Y4](const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B) {
		return 4 * (B.X - A.X) * (Y4 - 4 * A.Y) - 4 * (B.Y - A.Y) * (X4 - 4 * A.X);
	};
	return Side(T.A, T.B) > 0 && Side(T.B, T.C) > 0 && Side(T.C, T.A) > 0;
}

bool InsideOrBoundary(const FVoxelLandM5FloorTriangleCm& T, const FVoxelLandM5PointCm& P)
{
	return Orient(T.A, T.B, P) >= 0 && Orient(T.B, T.C, P) >= 0 && Orient(T.C, T.A, P) >= 0;
}

bool EdgePairCompatible(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, const FVoxelLandM5PointCm& C,
	const FVoxelLandM5PointCm& D)
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

bool OnPlane(const FVoxelLandM5PointCm& P, const FVoxelLandM5SupportPlane& Plane)
{
	return (P.Z - Plane.Origin.Z) * int64(Plane.Denominator)
		== (P.X - Plane.Origin.X) * int64(Plane.GradientX) + (P.Y - Plane.Origin.Y) * int64(Plane.GradientY);
}

bool SquareProduct(int64 A, int64 B, int64& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	int64 Square = 0;
	return A >= 0 && B >= 0 && TryMultiplyNonNegative(A, A, Square) && TryMultiplyNonNegative(Square, B, Out);
}
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
		return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
	}
	if (!InRange(FeetStart) || !InRange(FeetEnd) || !InRange(Plane.Origin) || RadiusCm < 1 || RadiusCm > Limit
		|| HeightCm < 2 * RadiusCm || HeightCm > Limit || ContactSkinCm < 0 || ContactSkinCm > Limit
		|| RadiusCm + ContactSkinCm > Limit || Plane.Denominator < 1 || Plane.Denominator > CoefficientLimit
		|| Plane.GradientX < -CoefficientLimit || Plane.GradientX > CoefficientLimit
		|| Plane.GradientY < -CoefficientLimit || Plane.GradientY > CoefficientLimit || MaxSlopeNumerator < 0
		|| MaxSlopeNumerator > CoefficientLimit || MaxSlopeDenominator < 1 || MaxSlopeDenominator > CoefficientLimit)
	{
		return Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid capsule, plane or slope"));
	}
	if (Triangles.IsEmpty())
	{
		return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("No support triangles"));
	}
	if (Triangles.Num() > 256)
	{
		return Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Too many support triangles"));
	}
	const int64 Count = Triangles.Num();
	// No scratch allocation. The fixed charge bounds all triangle/edge pair
	// comparisons and cancellation calls, including exact boundary scans.
	if (!Budget->TryChargeWork(128 + 256 * Count * Count))
	{
		return Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support work budget exhausted"));
	}
	const int64 GX = Plane.GradientX, GY = Plane.GradientY, Den = Plane.Denominator;
	const int64 SN = MaxSlopeNumerator, SD = MaxSlopeDenominator;
	if ((GX * GX + GY * GY) * SD * SD > Den * Den * SN * SN)
	{
		return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Support slope exceeds limit"));
	}
	const int64 NormalSquared = GX * GX + GY * GY + Den * Den;
	int64 MinSquared = 0, MaxSquared = 0;
	if (!SquareProduct(RadiusCm, NormalSquared, MinSquared)
		|| !SquareProduct(RadiusCm + ContactSkinCm, NormalSquared, MaxSquared))
	{
		return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Contact predicate exceeds exact arithmetic range"));
	}
	for (const FVoxelLandM5PointCm* Feet : {&FeetStart, &FeetEnd})
	{
		int64 TopZ = 0;
		if (!TryAdd(Feet->Z, HeightCm, TopZ) || !InRange(TopZ))
		{
			return Error(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Capsule exceeds domain"));
		}
		const int64 CenterZ = Feet->Z + RadiusCm;
		const int64 Dot =
			-(Feet->X - Plane.Origin.X) * GX - (Feet->Y - Plane.Origin.Y) * GY + (CenterZ - Plane.Origin.Z) * Den;
		int64 DotSquared = 0;
		if (Dot < 0 || !TryMultiplyNonNegative(Dot, Dot, DotSquared))
		{
			return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Capsule contact cannot be proven"));
		}
		if (DotSquared < MinSquared || DotSquared > MaxSquared)
		{
			return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Capsule penetrates or loses contact"));
		}
	}
	const int64 Extent = RadiusCm + ContactSkinCm;
	const int64 MinX = FMath::Min(FeetStart.X, FeetEnd.X) - Extent;
	const int64 MaxX = FMath::Max(FeetStart.X, FeetEnd.X) + Extent;
	const int64 MinY = FMath::Min(FeetStart.Y, FeetEnd.Y) - Extent;
	const int64 MaxY = FMath::Max(FeetStart.Y, FeetEnd.Y) + Extent;
	if (!InRange(MinX) || !InRange(MaxX) || !InRange(MinY) || !InRange(MaxY))
	{
		return Error(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Contact footprint exceeds domain"));
	}
	for (const FVoxelLandM5FloorTriangleCm& T : Triangles)
	{
		if (IsCancelled())
		{
			return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
		}
		if (!InRange(T.A) || !InRange(T.B) || !InRange(T.C) || !OnPlane(T.A, Plane) || !OnPlane(T.B, Plane)
			|| !OnPlane(T.C, Plane) || Orient(T.A, T.B, T.C) <= 0)
		{
			return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Invalid, noncoplanar or downward triangle"));
		}
	}
	for (int32 I = 0; I < Triangles.Num(); ++I)
	{
		for (int32 J = I + 1; J < Triangles.Num(); ++J)
		{
			if (IsCancelled())
			{
				return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
			}
			const auto& A = Triangles[I];
			const auto& B = Triangles[J];
			for (int32 K = 0; K < 3; ++K)
			{
				if (StrictlyInside(A, Vertex(B, K)) || StrictlyInside(B, Vertex(A, K)))
				{
					return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Overlapping floor triangles"));
				}
				for (int32 L = 0; L < 3; ++L)
				{
					if (!EdgePairCompatible(Vertex(A, K), Vertex(A, (K + 1) % 3), Vertex(B, L), Vertex(B, (L + 1) % 3)))
					{
						return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Intersecting or nonmanifold floor edges"));
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
			bCovered |= InsideOrBoundary(T, Corner);
		}
		if (!bCovered)
		{
			return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Contact footprint is not covered"));
		}
	}
	// Corners alone can touch four unrelated triangles outside the rectangle.
	// A strictly interior witness proves its open connected interior starts in
	// the triangle union. The boundary scan below then prevents leaving it.
	bool bInteriorWitness = false;
	const int64 CenterX4 = 2 * (MinX + MaxX);
	const int64 CenterY4 = 2 * (MinY + MaxY);
	// A triangulation edge may cross the center. Any one strictly interior
	// rational witness is sufficient for the boundary proof below.
	const int64 WitnessX4[] = {CenterX4, 3 * MinX + MaxX, MinX + 3 * MaxX, CenterX4, CenterX4};
	const int64 WitnessY4[] = {CenterY4, CenterY4, CenterY4, 3 * MinY + MaxY, MinY + 3 * MaxY};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WitnessX4); ++Index)
	{
		for (const auto& T : Triangles)
		{
			bInteriorWitness |= StrictlyInsideQuarter(T, WitnessX4[Index], WitnessY4[Index]);
		}
	}
	if (!bInteriorWitness)
	{
		return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("No strict interior support witness"));
	}
	for (int32 I = 0; I < Triangles.Num(); ++I)
	{
		for (int32 E = 0; E < 3; ++E)
		{
			if (IsCancelled())
			{
				return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
			}
			const auto& A = Vertex(Triangles[I], E);
			const auto& B = Vertex(Triangles[I], (E + 1) % 3);
			int32 ReverseCount = 0;
			for (int32 J = 0; J < Triangles.Num(); ++J)
			{
				if (IsCancelled())
				{
					return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
				}
				if (I == J)
				{
					continue;
				}
				for (int32 F = 0; F < 3; ++F)
				{
					if (EqualXY(A, Vertex(Triangles[J], (F + 1) % 3)) && EqualXY(B, Vertex(Triangles[J], F)))
					{
						++ReverseCount;
					}
				}
			}
			if (ReverseCount > 1)
			{
				return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Nonmanifold shared edge"));
			}
			if (ReverseCount == 0 && FMath::Min(A.X, B.X) < MaxX && FMath::Max(A.X, B.X) > MinX
				&& FMath::Min(A.Y, B.Y) < MaxY && FMath::Max(A.Y, B.Y) > MinY)
			{
				return Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Floor boundary may cross the contact footprint"));
			}
		}
	}
	if (IsCancelled())
	{
		return Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support check cancelled"));
	}
	return {};
}
