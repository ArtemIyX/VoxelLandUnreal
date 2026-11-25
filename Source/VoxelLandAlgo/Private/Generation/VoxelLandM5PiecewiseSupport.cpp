#include "Generation/VoxelLandM5PiecewiseSupport.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5FloorTriangleCm.h"
#include "Generation/VoxelLandM5SupportPlane.h"
#include "Generation/VoxelLandM5SupportMargin.h"
#include "Generation/VoxelLandM5TriangleSupport.h"

class FVoxelLandM5PiecewiseSupportInternals final
{
public:
	static constexpr int64 Limit = 256;
	static constexpr int64 Q18Limit = int64(1048576) << 18;
	static constexpr int64 Denominator = 256;
	struct FFace
	{
		FVoxelLandM5FloorTriangleCm Triangle;
		int64 NX = 0, NY = 0, NZ = 0;
	};
	struct FNode
	{
		int32 First = 0, Last = 256, Depth = 0;
	};
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("M5.PiecewiseSupport");
		D.Message = Message;
		return D;
	}
	static int64 Gcd(int64 A, int64 B)
	{
		A = FMath::Abs(A);
		B = FMath::Abs(B);
		while (B != 0)
		{
			const int64 R = A % B;
			A = B;
			B = R;
		}
		return A;
	}
	static bool Reduce(const FVoxelLandM5FixedPointQ18& P, const FVoxelLandM5FixedPointQ18& Origin, int64 Scale,
		FVoxelLandM5PointCm& Out)
	{
		if (P.X < -Q18Limit || P.X > Q18Limit || P.Y < -Q18Limit || P.Y > Q18Limit || P.Z < -Q18Limit || P.Z > Q18Limit)
		{
			return false;
		}
		const int64 X = P.X - Origin.X, Y = P.Y - Origin.Y, Z = P.Z - Origin.Z;
		if (X % Scale || Y % Scale || Z % Scale || FMath::Abs(X / Scale) > Limit || FMath::Abs(Y / Scale) > Limit
			|| FMath::Abs(Z / Scale) > Limit)
		{
			return false;
		}
		Out.X = X / Scale;
		Out.Y = Y / Scale;
		Out.Z = Z / Scale;
		return true;
	}
	static const FVoxelLandM5PointCm& Vertex(const FVoxelLandM5FloorTriangleCm& T, int32 I)
	{
		return I == 0 ? T.A : (I == 1 ? T.B : T.C);
	}
	static bool HaveCompatibleVertices(const FFace& A, const FFace& B)
	{
		for (int32 U = 0; U < 3; ++U)
		{
			for (int32 V = 0; V < 3; ++V)
			{
				const FVoxelLandM5PointCm& P = Vertex(A.Triangle, U);
				const FVoxelLandM5PointCm& Q = Vertex(B.Triangle, V);
				if (P.X == Q.X && P.Y == Q.Y && P.Z != Q.Z)
				{
					return false;
				}
			}
		}
		return true;
	}
	static int64 Gap(
		const FFace& F, const FVoxelLandM5PointCm& Start, const FVoxelLandM5PointCm& End, int64 Radius, int32 T)
	{
		const int64 X = Start.X * Denominator + (End.X - Start.X) * T - F.Triangle.A.X * Denominator;
		const int64 Y = Start.Y * Denominator + (End.Y - Start.Y) * T - F.Triangle.A.Y * Denominator;
		const int64 Z = (Start.Z + Radius) * Denominator + (End.Z - Start.Z) * T - F.Triangle.A.Z * Denominator;
		return F.NX * X + F.NY * Y + F.NZ * Z;
	}
	static bool ProjectedInside(
		const FFace& F, const FVoxelLandM5PointCm& Start, const FVoxelLandM5PointCm& End, int64 Radius, int32 T)
	{
		const int64 X = Start.X * Denominator + (End.X - Start.X) * T;
		const int64 Y = Start.Y * Denominator + (End.Y - Start.Y) * T;
		const int64 Z = (Start.Z + Radius) * Denominator + (End.Z - Start.Z) * T;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVoxelLandM5PointCm& A = Vertex(F.Triangle, I);
			const FVoxelLandM5PointCm& B = Vertex(F.Triangle, (I + 1) % 3);
			const int64 EX = B.X - A.X, EY = B.Y - A.Y, EZ = B.Z - A.Z;
			const int64 PX = X - A.X * Denominator, PY = Y - A.Y * Denominator, PZ = Z - A.Z * Denominator;
			// dot(cross(edge, center - vertex), normal) is invariant under
			// projection of the center along this face's normal.
			if ((EY * PZ - EZ * PY) * F.NX + (EZ * PX - EX * PZ) * F.NY + (EX * PY - EY * PX) * F.NZ < 0)
			{
				return false;
			}
		}
		return true;
	}
	static bool DistanceWithin(int64 GapValue, int64 Radius, int64 NormalSquared, bool bUpper)
	{
		using namespace VoxelLand::M5::CheckedMath;
		int64 A = 0, B = 0, C = 0;
		if (GapValue < 0 || !TryMultiplyNonNegative(GapValue, GapValue, A)
			|| !TryMultiplyNonNegative(Radius * Denominator, Radius * Denominator, B)
			|| !TryMultiplyNonNegative(B, NormalSquared, C))
		{
			return false;
		}
		return bUpper ? A <= C : A >= C;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::PiecewiseSupport::CertifyLinearContact(const FVoxelLandM5FixedPointQ18& StartQ,
	const FVoxelLandM5FixedPointQ18& EndQ, int64 RadiusQ18, int64 HeightQ18, int64 SkinQ18, int32 MaxSlopeNumerator,
	int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return CertifyLinearContactWithMargin(StartQ, EndQ, RadiusQ18, HeightQ18, SkinQ18, 0, MaxSlopeNumerator,
		MaxSlopeDenominator, Triangles, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::PiecewiseSupport::CertifyLinearContactWithMargin(
	const FVoxelLandM5FixedPointQ18& StartQ, const FVoxelLandM5FixedPointQ18& EndQ, int64 RadiusQ18, int64 HeightQ18,
	int64 SkinQ18, int64 MarginQ18, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled)
{
	using I = FVoxelLandM5PiecewiseSupportInternals;
	if (IsCancelled())
	{
		return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
	}
	if (Triangles.IsEmpty() || Triangles.Num() > 256 || RadiusQ18 <= 0 || RadiusQ18 > I::Q18Limit
		|| HeightQ18 < 2 * RadiusQ18 || HeightQ18 > I::Q18Limit || SkinQ18 < 0 || SkinQ18 > I::Q18Limit || MarginQ18 < 0
		|| MarginQ18 > I::Q18Limit || MaxSlopeNumerator < 0 || MaxSlopeNumerator > 32767 || MaxSlopeDenominator < 1
		|| MaxSlopeDenominator > 32767)
	{
		return I::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid support inputs"));
	}
	const FVoxelLandM5FixedPointQ18 Origin = Triangles[0].A;
	FVoxelLandM5PointCm Dummy;
	if (!I::Reduce(Origin, Origin, 1, Dummy))
	{
		return I::Error(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Support origin outside domain"));
	}
	const int64 Count = Triangles.Num();
	if (!Budget->TryChargeWork(128 + 256 * Count * Count))
	{
		return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support preprocessing budget exhausted"));
	}
	int64 Scale = 0;
	for (int32 Shift = 18; Shift >= 0; --Shift)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		const int64 S = int64(1) << Shift;
		if (RadiusQ18 % S || HeightQ18 % S || SkinQ18 % S || MarginQ18 % S || RadiusQ18 / S > I::Limit
			|| HeightQ18 / S > I::Limit || SkinQ18 / S > I::Limit || MarginQ18 / S > I::Limit
			|| !I::Reduce(StartQ, Origin, S, Dummy) || !I::Reduce(EndQ, Origin, S, Dummy))
		{
			continue;
		}
		bool bFits = true;
		for (const FVoxelLandM5FixedTriangleQ18& T : Triangles)
		{
			if (IsCancelled())
			{
				return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
			}
			if (T.RendererErrorQ18.X || T.RendererErrorQ18.Y || T.RendererErrorQ18.Z
				|| !I::Reduce(T.A, Origin, S, Dummy) || !I::Reduce(T.B, Origin, S, Dummy)
				|| !I::Reduce(T.C, Origin, S, Dummy))
			{
				bFits = false;
				break;
			}
		}
		if (bFits)
		{
			Scale = S;
			break;
		}
	}
	if (!Scale)
	{
		return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Exact local representation unavailable"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4096 + Count * int64(sizeof(I::FFace) + sizeof(FVoxelLandM5FloorTriangleCm)), Lease))
	{
		return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support scratch exhausted"));
	}
	TArray<I::FFace> Faces;
	TArray<FVoxelLandM5FloorTriangleCm> Flat;
	Faces.Reserve(Triangles.Num());
	Flat.Reserve(Triangles.Num());
	FVoxelLandM5PointCm Start, End;
	I::Reduce(StartQ, Origin, Scale, Start);
	I::Reduce(EndQ, Origin, Scale, End);
	const int64 Radius = RadiusQ18 / Scale, Height = HeightQ18 / Scale, Skin = SkinQ18 / Scale;
	for (const FVoxelLandM5FixedTriangleQ18& T : Triangles)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		I::FFace F;
		I::Reduce(T.A, Origin, Scale, F.Triangle.A);
		I::Reduce(T.B, Origin, Scale, F.Triangle.B);
		I::Reduce(T.C, Origin, Scale, F.Triangle.C);
		const FVoxelLandM5PointCm& A = F.Triangle.A;
		const FVoxelLandM5PointCm& B = F.Triangle.B;
		const FVoxelLandM5PointCm& C = F.Triangle.C;
		const int64 UX = B.X - A.X, UY = B.Y - A.Y, UZ = B.Z - A.Z, VX = C.X - A.X, VY = C.Y - A.Y, VZ = C.Z - A.Z;
		F.NX = UY * VZ - UZ * VY;
		F.NY = UZ * VX - UX * VZ;
		F.NZ = UX * VY - UY * VX;
		const int64 Divisor = I::Gcd(I::Gcd(F.NX, F.NY), F.NZ);
		if (F.NZ <= 0 || Divisor == 0)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Face is not upward"));
		}
		F.NX /= Divisor;
		F.NY /= Divisor;
		F.NZ /= Divisor;
		if (FMath::Abs(F.NX) > 32767 || FMath::Abs(F.NY) > 32767 || F.NZ > 32767
			|| (F.NX * F.NX + F.NY * F.NY) * int64(MaxSlopeDenominator) * MaxSlopeDenominator
				   > F.NZ * F.NZ * int64(MaxSlopeNumerator) * MaxSlopeNumerator)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Face slope or normal unsupported"));
		}
		Faces.Add(F);
		FVoxelLandM5FloorTriangleCm P = F.Triangle;
		P.A.Z = P.B.Z = P.C.Z = 0;
		Flat.Add(P);
	}
	for (int32 A = 0; A < Faces.Num(); ++A)
	{
		for (int32 B = A + 1; B < Faces.Num(); ++B)
		{
			if (IsCancelled())
			{
				return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
			}
			if (!I::HaveCompatibleVertices(Faces[A], Faces[B]))
			{
				return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Discontinuous mesh vertex"));
			}
		}
	}
	FVoxelLandM5PointCm FlatStart = Start, FlatEnd = End;
	FlatStart.Z = FlatEnd.Z = 0;
	FVoxelLandM5SupportPlane Plane;
	Plane.Denominator = 1;
	const FVoxelLandM5Diagnostic Coverage = VoxelLand::M5::TriangleSupport::CertifyLinearContact(
		FlatStart, FlatEnd, Radius, Height, Skin, Plane, 0, 1, Flat, Budget, IsCancelled);
	if (Coverage.Code != EVoxelLandM5ErrorCode::None)
	{
		return Coverage;
	}
	for (const I::FFace& F : Faces)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		if (MarginQ18 != 0)
		{
			if (!Budget->TryChargeWork(64))
			{
				return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support margin work exhausted"));
			}
			FVoxelLandM5SupportPlane FacePlane;
			FacePlane.Origin = F.Triangle.A;
			FacePlane.GradientX = int32(-F.NX);
			FacePlane.GradientY = int32(-F.NY);
			FacePlane.Denominator = int32(F.NZ);
			const FVoxelLandM5Diagnostic MarginResult =
				VoxelLand::M5::SupportMargin::Check(Start, End, Radius, MarginQ18 / Scale, FacePlane, IsCancelled);
			if (MarginResult.Code != EVoxelLandM5ErrorCode::None)
			{
				return MarginResult;
			}
		}
		const int64 N2 = F.NX * F.NX + F.NY * F.NY + F.NZ * F.NZ;
		if (!I::DistanceWithin(I::Gap(F, Start, End, Radius, 0), Radius, N2, false)
			|| !I::DistanceWithin(I::Gap(F, Start, End, Radius, 256), Radius, N2, false))
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Face nonpenetration unproven"));
		}
	}
	I::FNode Stack[9];
	int32 Size = 1, Visited = 0;
	while (Size)
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"));
		}
		if (++Visited > 511 || !Budget->TryChargeWork(64 + 64 * Count))
		{
			return I::Error(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Support subdivision budget exhausted"));
		}
		const I::FNode Node = Stack[--Size];
		bool bProven = false;
		for (const I::FFace& F : Faces)
		{
			const int64 N2 = F.NX * F.NX + F.NY * F.NY + F.NZ * F.NZ;
			if (I::ProjectedInside(F, Start, End, Radius, Node.First)
				&& I::ProjectedInside(F, Start, End, Radius, Node.Last)
				&& I::DistanceWithin(I::Gap(F, Start, End, Radius, Node.First), Radius + Skin, N2, true)
				&& I::DistanceWithin(I::Gap(F, Start, End, Radius, Node.Last), Radius + Skin, N2, true))
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
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Contact interval unresolved"));
		}
		const int32 Mid = (Node.First + Node.Last) / 2;
		Stack[Size++] = {Mid, Node.Last, Node.Depth + 1};
		Stack[Size++] = {Node.First, Mid, Node.Depth + 1};
	}
	return IsCancelled() ? I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Support cancelled"))
						 : FVoxelLandM5Diagnostic{};
}
