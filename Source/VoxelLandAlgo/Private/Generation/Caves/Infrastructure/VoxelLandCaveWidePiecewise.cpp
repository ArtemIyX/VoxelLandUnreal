#include "VoxelLandCaveWidePiecewise.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveWideInteger.h"

// Exact sign/magnitude arithmetic avoids relying on compiler-specific 128-bit types.
class FVoxelLandM5WidePiecewiseInternals final
{
public:
	using W = FVoxelLandM5WideInteger;
	static constexpr int64 Limit = int64(1048576) << 18;
	static constexpr int64 Denominator = 256;
	static constexpr int32 MaxFaces = 256;
	struct FNormal
	{
		W X;
		W Y;
		W Z;
		W Squared;
	};
	struct FNode
	{
		int32 First = 0;
		int32 Last = 256;
		int32 Depth = 0;
	};
	struct FRect
	{
		int64 MinX = 0;
		int64 MaxX = 0;
		int64 MinY = 0;
		int64 MaxY = 0;
	};

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.WidePiecewise");
		Result.Message = Message;
		return Result;
	}
	static bool InRange(int64 Value) { return Value >= -Limit && Value <= Limit; }
	static bool InRange(const FVoxelLandM5FixedPointQ18& P) { return InRange(P.X) && InRange(P.Y) && InRange(P.Z); }
	static const FVoxelLandM5FixedPointQ18& Vertex(const FVoxelLandM5FixedTriangleQ18& T, int32 Index)
	{
		return Index == 0 ? T.A : (Index == 1 ? T.B : T.C);
	}
	static bool EqualXY(const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B)
	{
		return A.X == B.X && A.Y == B.Y;
	}
	static bool Multiply(int64 A, int64 B, W& Out) { return W::TryMultiply(W::FromInt64(A), W::FromInt64(B), Out); }
	static bool DifferenceProducts(int64 A, int64 B, int64 C, int64 D, W& Out)
	{
		W First;
		W Second;
		return Multiply(A, B, First) && Multiply(C, D, Second) && W::TrySubtract(First, Second, Out);
	}
	static int32 Sign(const W& Value) { return W::Compare(Value, W()); }
	static bool Orient(const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B,
		const FVoxelLandM5FixedPointQ18& P, int32& Out)
	{
		W Value;
		if (!DifferenceProducts(B.X - A.X, P.Y - A.Y, B.Y - A.Y, P.X - A.X, Value))
		{
			return false;
		}
		Out = Sign(Value);
		return true;
	}
	static bool OnSegment(const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B,
		const FVoxelLandM5FixedPointQ18& P, bool& Out)
	{
		int32 Orientation = 0;
		if (!Orient(A, B, P, Orientation))
		{
			return false;
		}
		Out = Orientation == 0 && P.X >= FMath::Min(A.X, B.X) && P.X <= FMath::Max(A.X, B.X)
		   && P.Y >= FMath::Min(A.Y, B.Y) && P.Y <= FMath::Max(A.Y, B.Y);
		return true;
	}
	static bool Inside(
		const FVoxelLandM5FixedTriangleQ18& T, const FVoxelLandM5FixedPointQ18& P, bool bStrict, bool& Out)
	{
		int32 A = 0;
		int32 B = 0;
		int32 C = 0;
		if (!Orient(T.A, T.B, P, A) || !Orient(T.B, T.C, P, B) || !Orient(T.C, T.A, P, C))
		{
			return false;
		}
		Out = bStrict ? (A > 0 && B > 0 && C > 0) : (A >= 0 && B >= 0 && C >= 0);
		return true;
	}
	static bool EdgeCompatible(const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B,
		const FVoxelLandM5FixedPointQ18& C, const FVoxelLandM5FixedPointQ18& D, bool& Out)
	{
		if (EqualXY(A, D) && EqualXY(B, C))
		{
			Out = true;
			return true;
		}
		if (EqualXY(A, C) && EqualXY(B, D))
		{
			Out = false;
			return true;
		}
		int32 O1 = 0;
		int32 O2 = 0;
		int32 O3 = 0;
		int32 O4 = 0;
		if (!Orient(A, B, C, O1) || !Orient(A, B, D, O2) || !Orient(C, D, A, O3) || !Orient(C, D, B, O4))
		{
			return false;
		}
		if (O1 * O2 < 0 && O3 * O4 < 0)
		{
			Out = false;
			return true;
		}
		bool AC = false;
		bool AD = false;
		bool CA = false;
		bool CB = false;
		if (!OnSegment(A, B, C, AC) || !OnSegment(A, B, D, AD) || !OnSegment(C, D, A, CA) || !OnSegment(C, D, B, CB))
		{
			return false;
		}
		Out = !(AC && !EqualXY(C, A) && !EqualXY(C, B)) && !(AD && !EqualXY(D, A) && !EqualXY(D, B))
		   && !(CA && !EqualXY(A, C) && !EqualXY(A, D)) && !(CB && !EqualXY(B, C) && !EqualXY(B, D));
		return true;
	}
	static bool CompatibleEdges(const FVoxelLandM5FixedTriangleQ18& A, const FVoxelLandM5FixedTriangleQ18& B, int32 I)
	{
		const FVoxelLandM5FixedPointQ18& P = Vertex(A, I);
		for (int32 J = 0; J < 3; ++J)
		{
			const FVoxelLandM5FixedPointQ18& R = Vertex(B, J);
			if (EqualXY(P, R) && P.Z != R.Z)
			{
				return false;
			}
			bool bCompatible = false;
			if (!EdgeCompatible(P, Vertex(A, (I + 1) % 3), R, Vertex(B, (J + 1) % 3), bCompatible) || !bCompatible)
			{
				return false;
			}
		}
		return true;
	}
	static bool CompatiblePair(const FVoxelLandM5FixedTriangleQ18& A, const FVoxelLandM5FixedTriangleQ18& B)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			const FVoxelLandM5FixedPointQ18& P = Vertex(A, I);
			const FVoxelLandM5FixedPointQ18& Q = Vertex(B, I);
			bool bInsideA = false;
			bool bInsideB = false;
			if (!Inside(A, Q, true, bInsideA) || !Inside(B, P, true, bInsideB) || bInsideA || bInsideB
				|| !CompatibleEdges(A, B, I))
			{
				return false;
			}
		}
		return true;
	}
	static bool ContainsPoint(
		TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, const FVoxelLandM5FixedPointQ18& P)
	{
		for (const FVoxelLandM5FixedTriangleQ18& Triangle : Triangles)
		{
			bool bInside = false;
			if (!Inside(Triangle, P, false, bInside))
			{
				return false;
			}
			if (bInside)
			{
				return true;
			}
		}
		return false;
	}
	static bool EdgeCovered(
		TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, int32 Face, int32 Edge, const FRect& Bounds)
	{
		const FVoxelLandM5FixedPointQ18& A = Vertex(Triangles[Face], Edge);
		const FVoxelLandM5FixedPointQ18& B = Vertex(Triangles[Face], (Edge + 1) % 3);
		int32 ReverseCount = 0;
		for (int32 Other = 0; Other < Triangles.Num(); ++Other)
		{
			if (Other == Face)
			{
				continue;
			}
			for (int32 OtherEdge = 0; OtherEdge < 3; ++OtherEdge)
			{
				if (EqualXY(A, Vertex(Triangles[Other], (OtherEdge + 1) % 3))
					&& EqualXY(B, Vertex(Triangles[Other], OtherEdge)))
				{
					++ReverseCount;
				}
			}
		}
		if (ReverseCount > 1)
		{
			return false;
		}
		return ReverseCount == 1
			|| !(FMath::Min(A.X, B.X) < Bounds.MaxX && FMath::Max(A.X, B.X) > Bounds.MinX
				 && FMath::Min(A.Y, B.Y) < Bounds.MaxY && FMath::Max(A.Y, B.Y) > Bounds.MinY);
	}
	static bool Coverage(TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, const FRect& Bounds,
		TFunctionRef<bool()> IsCancelled, bool& bCancellationObserved)
	{
		for (int32 I = 0; I < Triangles.Num(); ++I)
		{
			for (int32 J = I + 1; J < Triangles.Num(); ++J)
			{
				if (IsCancelled())
				{
					bCancellationObserved = true;
					return false;
				}
				if (!CompatiblePair(Triangles[I], Triangles[J]))
				{
					return false;
				}
			}
		}
		FVoxelLandM5FixedPointQ18 P;
		for (int32 I = 0; I < 4; ++I)
		{
			P.X = (I & 1) != 0 ? Bounds.MaxX : Bounds.MinX;
			P.Y = (I & 2) != 0 ? Bounds.MaxY : Bounds.MinY;
			if (!ContainsPoint(Triangles, P))
			{
				return false;
			}
		}
		// Both ends and extents are integer Q18 values. Doubling all XY values
		// preserves exact midpoint testing even when the center is half a unit.
		TArray<FVoxelLandM5FixedTriangleQ18> Doubled;
		Doubled.Reserve(Triangles.Num());
		for (const FVoxelLandM5FixedTriangleQ18& Triangle : Triangles)
		{
			FVoxelLandM5FixedTriangleQ18 Copy = Triangle;
			Copy.A.X *= 2;
			Copy.A.Y *= 2;
			Copy.B.X *= 2;
			Copy.B.Y *= 2;
			Copy.C.X *= 2;
			Copy.C.Y *= 2;
			Doubled.Add(Copy);
		}
		P.X = Bounds.MinX + Bounds.MaxX;
		P.Y = Bounds.MinY + Bounds.MaxY;
		if (!ContainsPoint(Doubled, P))
		{
			return false;
		}
		for (int32 I = 0; I < Triangles.Num(); ++I)
		{
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				if (IsCancelled())
				{
					bCancellationObserved = true;
					return false;
				}
				if (!EdgeCovered(Triangles, I, Edge, Bounds))
				{
					return false;
				}
			}
		}
		return true;
	}
	static bool Square(const W& Value, W& Out) { return W::TryMultiply(Value, Value, Out); }
	static bool Add3(const W& A, const W& B, const W& C, W& Out)
	{
		W Sum;
		return W::TryAdd(A, B, Sum) && W::TryAdd(Sum, C, Out);
	}
	static bool Normal(const FVoxelLandM5FixedTriangleQ18& T, int32 SN, int32 SD, FNormal& Out)
	{
		const int64 UX = T.B.X - T.A.X;
		const int64 UY = T.B.Y - T.A.Y;
		const int64 UZ = T.B.Z - T.A.Z;
		const int64 VX = T.C.X - T.A.X;
		const int64 VY = T.C.Y - T.A.Y;
		const int64 VZ = T.C.Z - T.A.Z;
		if (!DifferenceProducts(UY, VZ, UZ, VY, Out.X) || !DifferenceProducts(UZ, VX, UX, VZ, Out.Y)
			|| !DifferenceProducts(UX, VY, UY, VX, Out.Z) || Sign(Out.Z) <= 0)
		{
			return false;
		}
		W X2;
		W Y2;
		W Z2;
		W XY;
		W Left;
		W Right;
		if (!Square(Out.X, X2) || !Square(Out.Y, Y2) || !Square(Out.Z, Z2) || !Add3(X2, Y2, Z2, Out.Squared)
			|| !W::TryAdd(X2, Y2, XY) || !W::TryMultiply(XY, W::FromInt64(int64(SD) * SD), Left)
			|| !W::TryMultiply(Z2, W::FromInt64(int64(SN) * SN), Right))
		{
			return false;
		}
		return W::Compare(Left, Right) <= 0;
	}
	static bool Dot(const FNormal& N, int64 X, int64 Y, int64 Z, W& Out)
	{
		W A;
		W B;
		W C;
		return W::TryMultiply(N.X, W::FromInt64(X), A) && W::TryMultiply(N.Y, W::FromInt64(Y), B)
			&& W::TryMultiply(N.Z, W::FromInt64(Z), C) && Add3(A, B, C, Out);
	}
	static bool Gap(const FNormal& N, const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& Start,
		const FVoxelLandM5FixedPointQ18& End, int64 Radius, int32 T, W& Out)
	{
		const int64 X = (Start.X - A.X) * Denominator + (End.X - Start.X) * T;
		const int64 Y = (Start.Y - A.Y) * Denominator + (End.Y - Start.Y) * T;
		const int64 Z = (Start.Z + Radius - A.Z) * Denominator + (End.Z - Start.Z) * T;
		return Dot(N, X, Y, Z, Out);
	}
	static bool InsideProjection(const FVoxelLandM5FixedTriangleQ18& Triangle, const FNormal& N,
		const FVoxelLandM5FixedPointQ18& Start, const FVoxelLandM5FixedPointQ18& End, int64 Radius, int32 T)
	{
		const int64 X = Start.X * Denominator + (End.X - Start.X) * T;
		const int64 Y = Start.Y * Denominator + (End.Y - Start.Y) * T;
		const int64 Z = (Start.Z + Radius) * Denominator + (End.Z - Start.Z) * T;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVoxelLandM5FixedPointQ18& A = Vertex(Triangle, I);
			const FVoxelLandM5FixedPointQ18& B = Vertex(Triangle, (I + 1) % 3);
			const int64 EX = B.X - A.X;
			const int64 EY = B.Y - A.Y;
			const int64 EZ = B.Z - A.Z;
			const int64 PX = X - A.X * Denominator;
			const int64 PY = Y - A.Y * Denominator;
			const int64 PZ = Z - A.Z * Denominator;
			W CX;
			W CY;
			W CZ;
			W DX;
			W DY;
			W DZ;
			W Side;
			if (!DifferenceProducts(EY, PZ, EZ, PY, CX) || !DifferenceProducts(EZ, PX, EX, PZ, CY)
				|| !DifferenceProducts(EX, PY, EY, PX, CZ) || !W::TryMultiply(CX, N.X, DX)
				|| !W::TryMultiply(CY, N.Y, DY) || !W::TryMultiply(CZ, N.Z, DZ) || !Add3(DX, DY, DZ, Side)
				|| Sign(Side) < 0)
			{
				return false;
			}
		}
		return true;
	}
	static bool DistanceWithin(const W& GapValue, int64 Radius, const W& NormalSquared, bool bUpper)
	{
		W GapSquared;
		W RadiusSquared;
		W Bound;
		const W ScaledRadius = W::FromInt64(Radius * Denominator);
		if (Sign(GapValue) < 0 || !Square(GapValue, GapSquared) || !Square(ScaledRadius, RadiusSquared)
			|| !W::TryMultiply(RadiusSquared, NormalSquared, Bound))
		{
			return false;
		}
		const int32 Order = W::Compare(GapSquared, Bound);
		return bUpper ? Order <= 0 : Order >= 0;
	}
	static bool Absolute(const W& Value, W& Out)
	{
		if (!Value.IsNegative())
		{
			Out = Value;
			return true;
		}
		return W::TrySubtract(W(), Value, Out);
	}
	static bool MarginEndpoint(const FNormal& N, const W& GapValue, int64 Radius, int64 Margin)
	{
		W AX;
		W AY;
		W Gradient;
		W Rise;
		W Vertical;
		W Nominal;
		W Inflated;
		W VerticalSquared;
		W NominalSquared;
		W InflatedSquared;
		W Sum;
		const int64 Reach = Radius + Margin;
		if (!Absolute(N.X, AX) || !Absolute(N.Y, AY) || !W::TryAdd(AX, AY, Gradient)
			|| !W::TryMultiply(Gradient, W::FromInt64(Reach * Denominator), Rise) || W::Compare(GapValue, Rise) <= 0
			|| !W::TrySubtract(GapValue, Rise, Vertical)
			|| !W::TryMultiply(N.Z, W::FromInt64(Radius * Denominator), Nominal)
			|| !W::TryMultiply(N.Z, W::FromInt64(Reach * Denominator), Inflated) || !Square(Vertical, VerticalSquared)
			|| !Square(Nominal, NominalSquared) || !Square(Inflated, InflatedSquared)
			|| !W::TryAdd(VerticalSquared, NominalSquared, Sum))
		{
			return false;
		}
		return W::Compare(Sum, InflatedSquared) > 0;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::WidePiecewise::Certify(const FVoxelLandM5FixedPointQ18& Start,
	const FVoxelLandM5FixedPointQ18& End, int64 Radius, int64 Height, int64 Skin, int64 Margin, int32 SlopeNumerator,
	int32 SlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	using I = FVoxelLandM5WidePiecewiseInternals;
	if (IsCancelled())
	{
		return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
	}
	if (Triangles.IsEmpty() || Triangles.Num() > I::MaxFaces || Radius <= 0 || Radius > I::Limit || Height < 2 * Radius
		|| Height > I::Limit || Skin < 0 || Skin > I::Limit || Margin < 0 || Margin > I::Limit || SlopeNumerator < 0
		|| SlopeNumerator > 32767 || SlopeDenominator < 1 || SlopeDenominator > 32767 || !I::InRange(Start)
		|| !I::InRange(End))
	{
		return I::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid wide support inputs"));
	}
	const int64 Extent = Radius + Skin;
	I::FRect Bounds;
	Bounds.MinX = FMath::Min(Start.X, End.X) - Extent;
	Bounds.MaxX = FMath::Max(Start.X, End.X) + Extent;
	Bounds.MinY = FMath::Min(Start.Y, End.Y) - Extent;
	Bounds.MaxY = FMath::Max(Start.Y, End.Y) + Extent;
	if (!I::InRange(Bounds.MinX) || !I::InRange(Bounds.MaxX) || !I::InRange(Bounds.MinY) || !I::InRange(Bounds.MaxY)
		|| !I::InRange(Start.Z + Height) || !I::InRange(End.Z + Height))
	{
		return I::Error(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Wide support envelope exceeds domain"));
	}
	const int64 Count = Triangles.Num();
	// Fixed limb products are bounded by 256 multiply-accumulates; charge the
	// full pairwise topology and face setup before any such work.
	if (!Budget->TryChargeWork(4096 + 65536 * Count + 65536 * Count * Count))
	{
		return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Wide support preprocessing exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	const int64 Bytes = 4096 + Count * int64(sizeof(I::FNormal) + sizeof(FVoxelLandM5FixedTriangleQ18));
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Wide support scratch exhausted"));
	}
	TArray<I::FNormal> Normals;
	Normals.Reserve(Triangles.Num());
	for (const FVoxelLandM5FixedTriangleQ18& Triangle : Triangles)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		if (!I::InRange(Triangle.A) || !I::InRange(Triangle.B) || !I::InRange(Triangle.C)
			|| Triangle.RendererErrorQ18.X != 0 || Triangle.RendererErrorQ18.Y != 0 || Triangle.RendererErrorQ18.Z != 0)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Wide support source is unsupported"));
		}
		I::FNormal Normal;
		if (!I::Normal(Triangle, SlopeNumerator, SlopeDenominator, Normal))
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Wide support face or slope is unsupported"));
		}
		Normals.Add(Normal);
	}
	bool bCancellationObserved = false;
	if (!I::Coverage(Triangles, Bounds, IsCancelled, bCancellationObserved))
	{
		return I::Error(bCancellationObserved ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::Unproven,
			TEXT("Wide support coverage unproven"));
	}
	for (int32 Face = 0; Face < Triangles.Num(); ++Face)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		I::W StartGap;
		I::W EndGap;
		if (!I::Gap(Normals[Face], Triangles[Face].A, Start, End, Radius, 0, StartGap)
			|| !I::Gap(Normals[Face], Triangles[Face].A, Start, End, Radius, 256, EndGap)
			|| !I::DistanceWithin(StartGap, Radius, Normals[Face].Squared, false)
			|| !I::DistanceWithin(EndGap, Radius, Normals[Face].Squared, false))
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Wide support nonpenetration unproven"));
		}
		if (Margin != 0
			&& (!I::MarginEndpoint(Normals[Face], StartGap, Radius, Margin)
				|| !I::MarginEndpoint(Normals[Face], EndGap, Radius, Margin)))
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Wide support margin unproven"));
		}
	}
	I::FNode Stack[9];
	int32 Size = 1;
	int32 Visited = 0;
	while (Size > 0)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		if (++Visited > 511 || !Budget->TryChargeWork(1024 + 32768 * Count))
		{
			return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Wide support subdivision exhausted"));
		}
		const I::FNode Node = Stack[--Size];
		bool bProven = false;
		for (int32 Face = 0; Face < Triangles.Num(); ++Face)
		{
			I::W FirstGap;
			I::W LastGap;
			if (I::InsideProjection(Triangles[Face], Normals[Face], Start, End, Radius, Node.First)
				&& I::InsideProjection(Triangles[Face], Normals[Face], Start, End, Radius, Node.Last)
				&& I::Gap(Normals[Face], Triangles[Face].A, Start, End, Radius, Node.First, FirstGap)
				&& I::Gap(Normals[Face], Triangles[Face].A, Start, End, Radius, Node.Last, LastGap)
				&& I::DistanceWithin(FirstGap, Radius + Skin, Normals[Face].Squared, true)
				&& I::DistanceWithin(LastGap, Radius + Skin, Normals[Face].Squared, true))
			{
				bProven = true;
				break;
			}
		}
		if (bProven)
		{
			continue;
		}
		if (Node.Depth == 8)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Wide support contact unresolved"));
		}
		const int32 Mid = (Node.First + Node.Last) / 2;
		Stack[Size++] = {Mid, Node.Last, Node.Depth + 1};
		Stack[Size++] = {Node.First, Mid, Node.Depth + 1};
	}
	return IsCancelled() ? I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"))
						 : FVoxelLandM5Diagnostic{};
}
