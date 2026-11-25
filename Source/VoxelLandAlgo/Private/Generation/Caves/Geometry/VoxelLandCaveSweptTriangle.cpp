#include "Generation/Caves/Geometry/VoxelLandCaveSweptTriangle.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"

#include <cfenv>
#include <cmath>
#include <limits>

class FVoxelLandM5SweptTriangleInternals final
{
public:
	static constexpr int64 M5SweepCoordinateLimit = 1048576;
	static constexpr int64 M5SweepWorkPerTriangle = 24;
	static constexpr int32 MaxSubdivisionDepth = 12;
	static constexpr int32 MaxSubdivisionNodes = 8191;
	static constexpr int64 StackAllowance = 4096;
	class FInterval final
	{
	public:
		int32 Index = 0;
		int32 Depth = 0;
	};
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
	static double M5DyadicCoordinate(int64 Start, int64 End, int32 Index, int32 Depth);
	static bool M5Separated(const FVector3d (&Medial)[4], const FVector3d& Path,
		const FVoxelLandM5ValidationTriangle& Triangle, const FVector3d& Error, double Radius);
	static void M5DyadicMedial(const FVoxelLandM5PointCm& Start, const FVoxelLandM5PointCm& End, int64 Radius,
		int64 Height, const FInterval& Interval, FVector3d (&Out)[4], FVector3d& OutPath);
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

// Numerators stay below 2^35; conversion and power-of-two scaling are exact.
double FVoxelLandM5SweptTriangleInternals::M5DyadicCoordinate(int64 Start, int64 End, int32 Index, int32 Depth)
{
	const int64 Scale = int64(1) << Depth;
	const int64 Numerator = Start * Scale + (End - Start) * Index;
	return std::ldexp(double(Numerator), -Depth);
}

void FVoxelLandM5SweptTriangleInternals::M5DyadicMedial(const FVoxelLandM5PointCm& Start,
	const FVoxelLandM5PointCm& End, int64 Radius, int64 Height, const FInterval& Interval, FVector3d (&Out)[4],
	FVector3d& OutPath)
{
	const int32 A = Interval.Index;
	const int32 B = A + 1;
	const int32 D = Interval.Depth;
	const double AX = M5DyadicCoordinate(Start.X, End.X, A, D);
	const double AY = M5DyadicCoordinate(Start.Y, End.Y, A, D);
	const double BX = M5DyadicCoordinate(Start.X, End.X, B, D);
	const double BY = M5DyadicCoordinate(Start.Y, End.Y, B, D);
	Out[0] = FVector3d(AX, AY, M5DyadicCoordinate(Start.Z + Radius, End.Z + Radius, A, D));
	Out[1] = FVector3d(AX, AY, M5DyadicCoordinate(Start.Z + Height - Radius, End.Z + Height - Radius, A, D));
	Out[2] = FVector3d(BX, BY, M5DyadicCoordinate(Start.Z + Radius, End.Z + Radius, B, D));
	Out[3] = FVector3d(BX, BY, M5DyadicCoordinate(Start.Z + Height - Radius, End.Z + Height - Radius, B, D));
	OutPath = Out[2] - Out[0];
}

bool FVoxelLandM5SweptTriangleInternals::M5Separated(const FVector3d (&Medial)[4], const FVector3d& Path,
	const FVoxelLandM5ValidationTriangle& Triangle, const FVector3d& Error, double Radius)
{
	const FVector3d Vertical(0.0, 0.0, 1.0);
	const FVector3d Edges[3] = {Triangle.B - Triangle.A, Triangle.C - Triangle.B, Triangle.A - Triangle.C};
	FVector3d Axes[23] = {FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0),
		M5Cross(Edges[0], Edges[1]), M5Cross(Path, Vertical)};
	int32 AxisCount = 5;
	for (const FVector3d& Edge : Edges)
	{
		Axes[AxisCount++] = M5Cross(Edge, Path);
		Axes[AxisCount++] = M5Cross(Edge, Vertical);
	}
	const FVector3d Vertices[3] = {Triangle.A, Triangle.B, Triangle.C};
	for (const FVector3d& Vertex : Vertices)
	{
		for (const FVector3d& Center : Medial)
		{
			Axes[AxisCount++] = Vertex - Center;
		}
	}
	for (int32 Index = 0; Index < AxisCount; ++Index)
	{
		if (M5AxisWitness(Axes[Index], Medial, Triangle, Error, Radius))
		{
			return true;
		}
	}
	return false;
}

FVoxelLandM5Diagnostic VoxelLand::M5::SweptTriangle::Check(const FVoxelLandM5PointCm& StartFeet,
	const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm, int64 MarginCm,
	const FVector3d& TriangleErrorCm, TConstArrayView<FVoxelLandM5ValidationTriangle> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return Check(StartFeet, EndFeet, RadiusCm, HeightCm, MarginCm, TriangleErrorCm, Triangles,
		FVoxelLandM5SweepSubdivisionLimits{}, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::SweptTriangle::Check(const FVoxelLandM5PointCm& StartFeet,
	const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm, int64 MarginCm,
	const FVector3d& TriangleErrorCm, TConstArrayView<FVoxelLandM5ValidationTriangle> Triangles,
	const FVoxelLandM5SweepSubdivisionLimits& Limits, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled)
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
	if (Limits.MaxDepth < 0 || Limits.MaxDepth > FVoxelLandM5SweptTriangleInternals::MaxSubdivisionDepth
		|| Limits.MaxNodes < 1 || Limits.MaxNodes > FVoxelLandM5SweptTriangleInternals::MaxSubdivisionNodes)
	{
		return FVoxelLandM5SweptTriangleInternals::M5SweepError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid sweep subdivision limits"));
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
	using I = FVoxelLandM5SweptTriangleInternals;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> StackLease;
	TArray<I::FInterval> Pending;
	const double InflatedRadius = double(RadiusCm + MarginCm);
	for (const FVoxelLandM5ValidationTriangle& Triangle : Triangles)
	{
		Pending.Reset();
		I::FInterval Current;
		int32 Visited = 0;
		while (true)
		{
			if (IsCancelled())
			{
				return I::M5SweepError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Sweep cancelled"));
			}
			// Callbacks can alter rounding or denormal controls on this thread.
			if (!I::M5SupportedEnvironment())
			{
				return I::M5SweepError(EVoxelLandM5ErrorCode::Unproven, TEXT("Floating-point environment changed"));
			}
			if (!I::M5FinitePoint(Triangle.A) || !I::M5FinitePoint(Triangle.B) || !I::M5FinitePoint(Triangle.C))
			{
				return I::M5SweepError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Triangle vertex outside supported domain"));
			}
			if (Visited >= Limits.MaxNodes)
			{
				return I::M5SweepError(EVoxelLandM5ErrorCode::Unproven, TEXT("Sweep subdivision node cap exhausted"));
			}
			if (!Budget->TryChargeWork(I::M5SweepWorkPerTriangle))
			{
				return I::M5SweepError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Sweep work budget exhausted"));
			}
			++Visited;
			FVector3d Medial[4];
			FVector3d Path;
			I::M5DyadicMedial(StartFeet, EndFeet, RadiusCm, HeightCm, Current, Medial, Path);
			if (I::M5Separated(Medial, Path, Triangle, TriangleErrorCm, InflatedRadius))
			{
				if (Pending.IsEmpty())
				{
					break;
				}
				Current = Pending.Pop(EAllowShrinking::No);
				continue;
			}
			if (Current.Depth >= Limits.MaxDepth || Visited >= Limits.MaxNodes)
			{
				return I::M5SweepError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Triangle separation could not be certified"));
			}
			if (!StackLease)
			{
				const int64 StackBytes = int64(Limits.MaxDepth + 1) * sizeof(I::FInterval) + I::StackAllowance;
				if (!Budget->TryReserveBytes(StackBytes, StackLease))
				{
					return I::M5SweepError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Sweep stack budget exhausted"));
				}
				Pending.Reserve(Limits.MaxDepth + 1);
			}
			I::FInterval Right;
			Right.Depth = Current.Depth + 1;
			Right.Index = Current.Index * 2 + 1;
			Pending.Add(Right);
			Current.Index *= 2;
			++Current.Depth;
		}
	}
	if (IsCancelled())
	{
		return I::M5SweepError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Sweep cancelled"));
	}
	if (!I::M5SupportedEnvironment())
	{
		return I::M5SweepError(EVoxelLandM5ErrorCode::Unproven, TEXT("Floating-point environment changed"));
	}
	return FVoxelLandM5Diagnostic{};
}
