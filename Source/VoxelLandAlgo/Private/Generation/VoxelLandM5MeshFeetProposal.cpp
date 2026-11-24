#include "Generation/VoxelLandM5MeshFeetProposal.h"

#include "Generation/VoxelLandM5Budget.h"
#include <cfenv>
#include <cmath>
#include <limits>

class FVoxelLandM5MeshFeetProposalInternals final
{
public:
	using FInterval = FVector2d;
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.MeshFeetProposal");
		Result.Message = Message;
		return Result;
	}
	static bool Environment()
	{
		if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
		{
			return false;
		}
		volatile double Min = std::numeric_limits<double>::min();
		volatile double Half = 0.5;
		volatile double Sub = Min * Half;
		volatile double Twice = Sub * 2.0;
		return Sub != 0.0 && Twice == Min;
	}
	static double Down(double X) { return std::nextafter(X, -std::numeric_limits<double>::infinity()); }
	static double Up(double X) { return std::nextafter(X, std::numeric_limits<double>::infinity()); }
	static double AddRaw(double A, double B) { volatile double V = A + B; return V; }
	static double SubRaw(double A, double B) { volatile double V = A - B; return V; }
	static double MulRaw(double A, double B) { volatile double V = A * B; return V; }
	static double DivRaw(double A, double B) { volatile double V = A / B; return V; }
	static FInterval Point(double X) { return {X, X}; }
	static FInterval Add(FInterval A, FInterval B) { return {Down(AddRaw(A.X, B.X)), Up(AddRaw(A.Y, B.Y))}; }
	static FInterval Sub(FInterval A, FInterval B) { return {Down(SubRaw(A.X, B.Y)), Up(SubRaw(A.Y, B.X))}; }
	static FInterval Mul(FInterval A, FInterval B)
	{
		const double Values[] = {MulRaw(A.X, B.X), MulRaw(A.X, B.Y), MulRaw(A.Y, B.X), MulRaw(A.Y, B.Y)};
		const double Lo = FMath::Min(FMath::Min(Values[0], Values[1]), FMath::Min(Values[2], Values[3]));
		const double Hi = FMath::Max(FMath::Max(Values[0], Values[1]), FMath::Max(Values[2], Values[3]));
		return {Down(Lo), Up(Hi)};
	}
	static FInterval DivPositive(FInterval A, FInterval B)
	{
		const double Values[] = {DivRaw(A.X, B.X), DivRaw(A.X, B.Y), DivRaw(A.Y, B.X), DivRaw(A.Y, B.Y)};
		const double Lo = FMath::Min(FMath::Min(Values[0], Values[1]), FMath::Min(Values[2], Values[3]));
		const double Hi = FMath::Max(FMath::Max(Values[0], Values[1]), FMath::Max(Values[2], Values[3]));
		return {Down(Lo), Up(Hi)};
	}
	static FInterval Cross(FInterval AX, FInterval AY, FInterval BX, FInterval BY)
	{
		return Sub(Mul(AX, BY), Mul(AY, BX));
	}
	static bool Finite(FInterval V) { return std::isfinite(V.X) && std::isfinite(V.Y) && V.X <= V.Y; }
};

FVoxelLandM5Diagnostic VoxelLand::M5::MeshFeetProposal::Build(const FVoxelLandM5PointCm& Center, int64 LowerZCm,
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5PointCm& OutFeet)
{
	using I = FVoxelLandM5MeshFeetProposalInternals;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Mesh feet proposal cancelled"));
	}
	constexpr int64 Limit = 1048576;
	if (Center.X < -Limit || Center.X > Limit || Center.Y < -Limit || Center.Y > Limit
		|| Center.Z < -Limit || Center.Z > Limit || LowerZCm < -Limit || LowerZCm > Center.Z
		|| Triangles.Num() > 196608)
	{
		return I::Error(E::InvalidInput, TEXT("Invalid mesh feet proposal inputs"));
	}
	if (!I::Environment())
	{
		return I::Error(E::Unproven, TEXT("Unsupported floating-point environment"));
	}
	if (!Budget->TryChargeWork(64 + int64(Triangles.Num()) * 128))
	{
		return I::Error(E::BudgetExceeded, TEXT("Mesh feet proposal work exhausted"));
	}
	bool bFound = false;
	I::FInterval Best{0.0, 0.0};
	int64 BestFeetZ = 0;
	for (const FVoxelLandM5FixedTriangleQ18& T : Triangles)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Mesh feet proposal cancelled"));
		}
		if (!I::Environment())
		{
			return I::Error(E::Unproven, TEXT("Floating-point environment changed"));
		}
		if (T.SourceTriangle < 0 || T.RendererErrorQ18.X != 0 || T.RendererErrorQ18.Y != 0
			|| T.RendererErrorQ18.Z != 0)
		{
			return I::Error(E::Unproven, TEXT("Invalid triangle identity or renderer uncertainty"));
		}
		constexpr int64 FixedLimit = int64(1048576) << 18;
		const FVoxelLandM5FixedPointQ18 Fixed[] = {T.A, T.B, T.C};
		FVector3d V[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVoxelLandM5FixedPointQ18& P = Fixed[Index];
			if (P.X < -FixedLimit || P.X > FixedLimit || P.Y < -FixedLimit || P.Y > FixedLimit
				|| P.Z < -FixedLimit || P.Z > FixedLimit)
			{
				return I::Error(E::Unproven, TEXT("Unsupported triangle coordinate"));
			}
			// Bounded 38-bit integers are exactly representable; division by
			// 2^18 is exact binary scaling under the verified environment.
			V[Index] = FVector3d(double(P.X) / 262144.0, double(P.Y) / 262144.0, double(P.Z) / 262144.0);
		}
		const double X = double(Center.X), Y = double(Center.Y);
		if (X < FMath::Min3(V[0].X, V[1].X, V[2].X) || X > FMath::Max3(V[0].X, V[1].X, V[2].X)
			|| Y < FMath::Min3(V[0].Y, V[1].Y, V[2].Y) || Y > FMath::Max3(V[0].Y, V[1].Y, V[2].Y)
			|| double(LowerZCm) > FMath::Max3(V[0].Z, V[1].Z, V[2].Z)
			|| double(Center.Z) < FMath::Min3(V[0].Z, V[1].Z, V[2].Z))
		{
			continue;
		}
		if ((V[0].X == V[1].X && V[1].X == V[2].X) || (V[0].Y == V[1].Y && V[1].Y == V[2].Y))
		{
			continue;
		}
		const I::FInterval UX = I::Sub(I::Point(V[1].X), I::Point(V[0].X));
		const I::FInterval UY = I::Sub(I::Point(V[1].Y), I::Point(V[0].Y));
		const I::FInterval VX = I::Sub(I::Point(V[2].X), I::Point(V[0].X));
		const I::FInterval VY = I::Sub(I::Point(V[2].Y), I::Point(V[0].Y));
		const I::FInterval PX = I::Sub(I::Point(X), I::Point(V[0].X));
		const I::FInterval PY = I::Sub(I::Point(Y), I::Point(V[0].Y));
		const I::FInterval Area = I::Cross(UX, UY, VX, VY);
		if (!I::Finite(Area))
		{
			return I::Error(E::Unproven, TEXT("Projected triangle arithmetic overflow"));
		}
		if (Area.Y < 0.0)
		{
			continue;
		}
		if (Area.X <= 0.0)
		{
			return I::Error(E::Unproven, TEXT("Ambiguous projected triangle orientation"));
		}
		const I::FInterval B = I::Cross(PX, PY, VX, VY);
		const I::FInterval C = I::Cross(UX, UY, PX, PY);
		const I::FInterval A = I::Sub(I::Sub(Area, B), C);
		if (!I::Finite(A) || !I::Finite(B) || !I::Finite(C))
		{
			return I::Error(E::Unproven, TEXT("Triangle membership arithmetic overflow"));
		}
		if (A.Y < 0.0 || B.Y < 0.0 || C.Y < 0.0)
		{
			continue;
		}
		if (A.X <= 0.0 || B.X <= 0.0 || C.X <= 0.0)
		{
			return I::Error(E::Unproven, TEXT("Ambiguous triangle boundary"));
		}
		I::FInterval Height;
		if (V[0].Z == V[1].Z && V[1].Z == V[2].Z)
		{
			Height = I::Point(V[0].Z);
		}
		else
		{
			const I::FInterval BZ = I::Sub(I::Point(V[1].Z), I::Point(V[0].Z));
			const I::FInterval CZ = I::Sub(I::Point(V[2].Z), I::Point(V[0].Z));
			Height = I::Add(I::Point(V[0].Z), I::DivPositive(I::Add(I::Mul(B, BZ), I::Mul(C, CZ)), Area));
		}
		if (!I::Finite(Height))
		{
			return I::Error(E::Unproven, TEXT("Triangle height arithmetic overflow"));
		}
		if (Height.Y < double(LowerZCm) || Height.X > double(Center.Z))
		{
			continue;
		}
		if (Height.X < double(LowerZCm) || Height.Y > double(Center.Z))
		{
			return I::Error(E::Unproven, TEXT("Intersection touches vertical limit ambiguously"));
		}
		const double LowCeil = std::ceil(Height.X), HighCeil = std::ceil(Height.Y);
		if (LowCeil != HighCeil || LowCeil < -double(Limit) || LowCeil > double(Limit))
		{
			return I::Error(E::Unproven, TEXT("Intersection cannot be rounded uniquely"));
		}
		if (bFound && !(Height.X > Best.Y || Height.Y < Best.X))
		{
			return I::Error(E::Unproven, TEXT("Overlapping surface height intervals"));
		}
		if (!bFound || Height.X > Best.Y)
		{
			Best = Height;
			BestFeetZ = int64(LowCeil);
			bFound = true;
		}
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Mesh feet proposal cancelled"));
	}
	if (!I::Environment() || !bFound)
	{
		return I::Error(E::Unproven, TEXT("No unambiguous upward intersection"));
	}
	FVoxelLandM5PointCm Result = Center;
	Result.Z = BestFeetZ;
	OutFeet = Result;
	return {};
}
