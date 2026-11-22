#include "Generation/VoxelLandM5SweptTriangle.h"

#include "Generation/VoxelLandM5Budget.h"

#include <cfenv>
#include <cmath>
#include <limits>

class FVoxelLandM5SweptTriangleInternals final
{
public:
	static constexpr int64 M5SweepCoordinateLimit = 1048576;
	static constexpr int64 M5SweepWorkPerTriangle = 24;
	static FVoxelLandM5Diagnostic M5SweepError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static double M5Down(double X);
	static double M5Up(double X);
	static double M5Add(double A, double B);
	static double M5Subtract(double A, double B);
	static double M5Multiply(double A, double B);
	static bool M5SupportedEnvironment();
	static bool M5FiniteCoordinate(double X);
	static bool M5FinitePoint(const FVector3d& P);
	static bool M5ValidFeet(const FVoxelLandM5PointCm& P);
	static bool M5AxisWitness(const FVector3d& Axis, const FVector3d (&Medial)[4],
		const FVoxelLandM5ValidationTriangle& Triangle, const FVector3d& Error, double Radius);
	static FVector3d M5Cross(const FVector3d& A, const FVector3d& B);
};

// Covers construction of all 23 candidate axes and their interval witnesses.
// A unit is a bounded witness-operation group, not an allocator measurement.
FVoxelLandM5Diagnostic FVoxelLandM5SweptTriangleInternals::M5SweepError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.SweptTriangle");
	Result.Message = Message;
	return Result;
}

// The operands are finite binary64 values. Volatile barriers prohibit FMA or
// reassociation across each operation. Under round-to-nearest, one nextafter
// step bounds the exact operation. Gradual underflow is checked before use.
double FVoxelLandM5SweptTriangleInternals::M5Down(double X)
{
	return std::nextafter(X, -std::numeric_limits<double>::infinity());
}
double FVoxelLandM5SweptTriangleInternals::M5Up(double X)
{
	return std::nextafter(X, std::numeric_limits<double>::infinity());
}
double FVoxelLandM5SweptTriangleInternals::M5Add(double A, double B)
{
	volatile double Value = A + B;
	return Value;
}
double FVoxelLandM5SweptTriangleInternals::M5Subtract(double A, double B)
{
	volatile double Value = A - B;
	return Value;
}
double FVoxelLandM5SweptTriangleInternals::M5Multiply(double A, double B)
{
	volatile double Value = A * B;
	return Value;
}

bool FVoxelLandM5SweptTriangleInternals::M5SupportedEnvironment()
{
	if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
	{
		return false;
	}
	volatile double MinNormal = std::numeric_limits<double>::min();
	volatile double Half = 0.5;
	volatile double Subnormal = MinNormal * Half;
	volatile double Twice = Subnormal * 2.0;
	return Subnormal != 0.0 && Twice == MinNormal;
}

bool FVoxelLandM5SweptTriangleInternals::M5FiniteCoordinate(double X)
{
	return std::isfinite(X) && std::abs(X) <= double(M5SweepCoordinateLimit);
}
bool FVoxelLandM5SweptTriangleInternals::M5FinitePoint(const FVector3d& P)
{
	return M5FiniteCoordinate(P.X) && M5FiniteCoordinate(P.Y) && M5FiniteCoordinate(P.Z);
}
bool FVoxelLandM5SweptTriangleInternals::M5ValidFeet(const FVoxelLandM5PointCm& P)
{
	return P.X >= -M5SweepCoordinateLimit && P.X <= M5SweepCoordinateLimit && P.Y >= -M5SweepCoordinateLimit
		&& P.Y <= M5SweepCoordinateLimit && P.Z >= -M5SweepCoordinateLimit && P.Z <= M5SweepCoordinateLimit;
}

// Axis is an arbitrary exact binary64 vector, not a claimed exact geometric
// normal. Its construction may round: any vector with a verified strict gap is
// a valid separating witness. Normalization is deliberately unnecessary.
bool FVoxelLandM5SweptTriangleInternals::M5AxisWitness(const FVector3d& Axis, const FVector3d (&Medial)[4],
	const FVoxelLandM5ValidationTriangle& Triangle, const FVector3d& Error, double Radius)
{
	if (!std::isfinite(Axis.X) || !std::isfinite(Axis.Y) || !std::isfinite(Axis.Z)
		|| (Axis.X == 0.0 && Axis.Y == 0.0 && Axis.Z == 0.0))
	{
		return false;
	}
	// Scale to avoid overflow. The rounded scaled components themselves are the
	// exact candidate axis; the original unscaled axis is never used as proof.
	const double Scale = FMath::Max3(std::abs(Axis.X), std::abs(Axis.Y), std::abs(Axis.Z));
	volatile double NX = Axis.X / Scale;
	volatile double NY = Axis.Y / Scale;
	volatile double NZ = Axis.Z / Scale;
	const double N[3] = {NX, NY, NZ};
	if (!std::isfinite(N[0]) || !std::isfinite(N[1]) || !std::isfinite(N[2]))
	{
		return false;
	}
	const FVector3d Vertices[3] = {Triangle.A, Triangle.B, Triangle.C};
	double TLow = std::numeric_limits<double>::infinity();
	double THigh = -std::numeric_limits<double>::infinity();
	for (const FVector3d& V : Vertices)
	{
		const double Position[3] = {V.X, V.Y, V.Z};
		const double Uncertainty[3] = {Error.X, Error.Y, Error.Z};
		double Low = 0.0;
		double High = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			const double A = M5Down(M5Subtract(Position[I], Uncertainty[I]));
			const double B = M5Up(M5Add(Position[I], Uncertainty[I]));
			const double PLow = M5Down(M5Multiply(N[I], N[I] >= 0.0 ? A : B));
			const double PHigh = M5Up(M5Multiply(N[I], N[I] >= 0.0 ? B : A));
			Low = M5Down(M5Add(Low, PLow));
			High = M5Up(M5Add(High, PHigh));
		}
		TLow = FMath::Min(TLow, Low);
		THigh = FMath::Max(THigh, High);
	}
	double PLow = std::numeric_limits<double>::infinity();
	double PHigh = -std::numeric_limits<double>::infinity();
	for (const FVector3d& P : Medial)
	{
		const double Position[3] = {P.X, P.Y, P.Z};
		double Low = 0.0;
		double High = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			const double Product = M5Multiply(N[I], Position[I]);
			Low = M5Down(M5Add(Low, M5Down(Product)));
			High = M5Up(M5Add(High, M5Up(Product)));
		}
		PLow = FMath::Min(PLow, Low);
		PHigh = FMath::Max(PHigh, High);
	}
	const double Gap = FMath::Max(M5Down(M5Subtract(TLow, PHigh)), M5Down(M5Subtract(PLow, THigh)));
	if (!(Gap > 0.0) || !std::isfinite(Gap))
	{
		return false;
	}
	double LengthSquaredUpper = 0.0;
	for (double Component : N)
	{
		LengthSquaredUpper = M5Up(M5Add(LengthSquaredUpper, M5Up(M5Multiply(Component, Component))));
	}
	const double GapSquaredLower = M5Down(M5Multiply(Gap, Gap));
	const double RadiusSquaredUpper = M5Up(M5Multiply(Radius, Radius));
	const double BoundUpper = M5Up(M5Multiply(RadiusSquaredUpper, LengthSquaredUpper));
	return std::isfinite(GapSquaredLower) && std::isfinite(BoundUpper) && GapSquaredLower > BoundUpper;
}

FVector3d FVoxelLandM5SweptTriangleInternals::M5Cross(const FVector3d& A, const FVector3d& B)
{
	return FVector3d(M5Subtract(M5Multiply(A.Y, B.Z), M5Multiply(A.Z, B.Y)),
		M5Subtract(M5Multiply(A.Z, B.X), M5Multiply(A.X, B.Z)), M5Subtract(M5Multiply(A.X, B.Y), M5Multiply(A.Y, B.X)));
}

FVoxelLandM5Diagnostic VoxelLand::M5::SweptTriangle::Check(const FVoxelLandM5PointCm& StartFeet,
	const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm, int64 MarginCm,
	const FVector3d& TriangleErrorCm, TConstArrayView<FVoxelLandM5ValidationTriangle> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Sweep cancelled"));
	}
	if (!FVoxelLandM5SweptTriangleInternals::M5SupportedEnvironment())
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Unsupported floating-point environment"));
	}
	if (!FVoxelLandM5SweptTriangleInternals::M5ValidFeet(StartFeet)
		|| !FVoxelLandM5SweptTriangleInternals::M5ValidFeet(EndFeet) || RadiusCm <= 0
		|| RadiusCm > FVoxelLandM5SweptTriangleInternals::M5SweepCoordinateLimit || HeightCm < 2 * RadiusCm
		|| HeightCm > FVoxelLandM5SweptTriangleInternals::M5SweepCoordinateLimit || MarginCm < 0
		|| MarginCm > FVoxelLandM5SweptTriangleInternals::M5SweepCoordinateLimit
		|| !FVoxelLandM5SweptTriangleInternals::M5FinitePoint(TriangleErrorCm) || TriangleErrorCm.X < 0.0
		|| TriangleErrorCm.Y < 0.0 || TriangleErrorCm.Z < 0.0)
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid sweep dimensions or error bounds"));
	}
	// Exact integer additions are safe under the validated bounds; verify the
	// medial centers remain inside the declared logical coordinate envelope.
	if (StartFeet.Z > FVoxelLandM5SweptTriangleInternals::M5SweepCoordinateLimit - (HeightCm - RadiusCm)
		|| EndFeet.Z > FVoxelLandM5SweptTriangleInternals::M5SweepCoordinateLimit - (HeightCm - RadiusCm))
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Capsule medial endpoints outside domain"));
	}
	if (!Budget->TryChargeWork(int64(Triangles.Num()) * FVoxelLandM5SweptTriangleInternals::M5SweepWorkPerTriangle))
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Sweep work budget exhausted"));
	}
	const FVector3d Medial[4] = {FVector3d(double(StartFeet.X), double(StartFeet.Y), double(StartFeet.Z + RadiusCm)),
		FVector3d(double(StartFeet.X), double(StartFeet.Y), double(StartFeet.Z + HeightCm - RadiusCm)),
		FVector3d(double(EndFeet.X), double(EndFeet.Y), double(EndFeet.Z + RadiusCm)),
		FVector3d(double(EndFeet.X), double(EndFeet.Y), double(EndFeet.Z + HeightCm - RadiusCm))};
	const FVector3d Path(
		double(EndFeet.X - StartFeet.X), double(EndFeet.Y - StartFeet.Y), double(EndFeet.Z - StartFeet.Z));
	const FVector3d Vertical(0.0, 0.0, 1.0);
	const double InflatedRadius = double(RadiusCm + MarginCm);
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SweptTriangleInternals::M5SweepError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Sweep cancelled"));
		}
		// A callback can change thread-local rounding or denormal controls.
		// Recheck after every callback before evaluating any witness.
		if (!FVoxelLandM5SweptTriangleInternals::M5SupportedEnvironment())
		{
			return FVoxelLandM5SweptTriangleInternals::M5SweepError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Floating-point environment changed"));
		}
		const FVoxelLandM5ValidationTriangle& T = Triangles[Index];
		if (!FVoxelLandM5SweptTriangleInternals::M5FinitePoint(T.A)
			|| !FVoxelLandM5SweptTriangleInternals::M5FinitePoint(T.B)
			|| !FVoxelLandM5SweptTriangleInternals::M5FinitePoint(T.C))
		{
			return FVoxelLandM5SweptTriangleInternals::M5SweepError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Triangle vertex outside supported domain"));
		}
		const FVector3d Edges[3] = {T.B - T.A, T.C - T.B, T.A - T.C};
		FVector3d Axes[23] = {FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0),
			FVoxelLandM5SweptTriangleInternals::M5Cross(Edges[0], Edges[1]),
			FVoxelLandM5SweptTriangleInternals::M5Cross(Path, Vertical)};
		int32 AxisCount = 5;
		for (const FVector3d& Edge : Edges)
		{
			Axes[AxisCount++] = FVoxelLandM5SweptTriangleInternals::M5Cross(Edge, Path);
			Axes[AxisCount++] = FVoxelLandM5SweptTriangleInternals::M5Cross(Edge, Vertical);
		}
		const FVector3d Vertices[3] = {T.A, T.B, T.C};
		for (const FVector3d& V : Vertices)
		{
			for (const FVector3d& P : Medial)
			{
				Axes[AxisCount++] = V - P;
			}
		}
		bool bSeparated = false;
		for (int32 AxisIndex = 0; AxisIndex < AxisCount; ++AxisIndex)
		{
			if (FVoxelLandM5SweptTriangleInternals::M5AxisWitness(
					Axes[AxisIndex], Medial, T, TriangleErrorCm, InflatedRadius))
			{
				bSeparated = true;
				break;
			}
		}
		if (!bSeparated)
		{
			return FVoxelLandM5SweptTriangleInternals::M5SweepError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Triangle separation could not be certified"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Sweep cancelled"));
	}
	return FVoxelLandM5Diagnostic{};
}
