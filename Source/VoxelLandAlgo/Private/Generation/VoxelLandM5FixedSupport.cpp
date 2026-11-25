#include "Generation/VoxelLandM5FixedSupport.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5FloorTriangleCm.h"
#include "Generation/VoxelLandM5SupportPlane.h"
#include "Generation/VoxelLandM5SupportMargin.h"
#include "Generation/VoxelLandM5TriangleSupport.h"

class FVoxelLandM5FixedSupportInternals final
{
public:
	static constexpr int64 Q18Limit = int64(1048576) << 18;
	static constexpr int64 ReducedLimit = 1048576;
	static FVoxelLandM5Diagnostic FixedSupportError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static bool InQ18Range(const FVoxelLandM5FixedPointQ18& P);
	static bool ReducePoint(const FVoxelLandM5FixedPointQ18& P, const FVoxelLandM5FixedPointQ18& Origin, int64 Scale,
		FVoxelLandM5PointCm& Out);
	static int64 GcdPositive(int64 A, int64 B);
	static FVoxelLandM5Diagnostic Certify(const FVoxelLandM5FixedPointQ18& FeetStart,
		const FVoxelLandM5FixedPointQ18& FeetEnd, int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18,
		int64 MarginQ18, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
		TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
};

FVoxelLandM5Diagnostic FVoxelLandM5FixedSupportInternals::FixedSupportError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.FixedSupport");
	Result.Message = Message;
	return Result;
}

bool FVoxelLandM5FixedSupportInternals::InQ18Range(const FVoxelLandM5FixedPointQ18& P)
{
	return P.X >= -Q18Limit && P.X <= Q18Limit && P.Y >= -Q18Limit && P.Y <= Q18Limit && P.Z >= -Q18Limit
		&& P.Z <= Q18Limit;
}

bool FVoxelLandM5FixedSupportInternals::ReducePoint(
	const FVoxelLandM5FixedPointQ18& P, const FVoxelLandM5FixedPointQ18& Origin, int64 Scale, FVoxelLandM5PointCm& Out)
{
	// Inputs are within +/- 2^38, so these differences are safely in int64.
	const int64 X = P.X - Origin.X, Y = P.Y - Origin.Y, Z = P.Z - Origin.Z;
	if (X % Scale != 0 || Y % Scale != 0 || Z % Scale != 0)
	{
		return false;
	}
	const int64 RX = X / Scale, RY = Y / Scale, RZ = Z / Scale;
	if (RX < -ReducedLimit || RX > ReducedLimit || RY < -ReducedLimit || RY > ReducedLimit || RZ < -ReducedLimit
		|| RZ > ReducedLimit)
	{
		return false;
	}
	Out.X = RX;
	Out.Y = RY;
	Out.Z = RZ;
	return true;
}

int64 FVoxelLandM5FixedSupportInternals::GcdPositive(int64 A, int64 B)
{
	// Cross products below are bounded by 2^43; negation cannot hit MIN_int64.
	A = A < 0 ? -A : A;
	B = B < 0 ? -B : B;
	while (B != 0)
	{
		const int64 R = A % B;
		A = B;
		B = R;
	}
	return A;
}

FVoxelLandM5Diagnostic VoxelLand::M5::FixedSupport::CertifyLinearContact(const FVoxelLandM5FixedPointQ18& FeetStart,
	const FVoxelLandM5FixedPointQ18& FeetEnd, int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18,
	int32 MaxSlopeNumerator, int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return FVoxelLandM5FixedSupportInternals::Certify(FeetStart, FeetEnd, RadiusQ18, HeightQ18, ContactSkinQ18,
		0, MaxSlopeNumerator, MaxSlopeDenominator, Triangles, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic VoxelLand::M5::FixedSupport::CertifyLinearContactWithMargin(
	const FVoxelLandM5FixedPointQ18& FeetStart, const FVoxelLandM5FixedPointQ18& FeetEnd,
	int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18, int64 MarginQ18,
	int32 MaxSlopeNumerator, int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	return FVoxelLandM5FixedSupportInternals::Certify(FeetStart, FeetEnd, RadiusQ18, HeightQ18, ContactSkinQ18,
		MarginQ18, MaxSlopeNumerator, MaxSlopeDenominator, Triangles, Budget, IsCancelled);
}

FVoxelLandM5Diagnostic FVoxelLandM5FixedSupportInternals::Certify(const FVoxelLandM5FixedPointQ18& FeetStart,
	const FVoxelLandM5FixedPointQ18& FeetEnd, int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18,
	int64 MarginQ18, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
	}
	if (!FVoxelLandM5FixedSupportInternals::InQ18Range(FeetStart)
		|| !FVoxelLandM5FixedSupportInternals::InQ18Range(FeetEnd) || RadiusQ18 <= 0
		|| RadiusQ18 > FVoxelLandM5FixedSupportInternals::Q18Limit || HeightQ18 < 2 * RadiusQ18
		|| HeightQ18 > FVoxelLandM5FixedSupportInternals::Q18Limit || ContactSkinQ18 < 0
		|| ContactSkinQ18 > FVoxelLandM5FixedSupportInternals::Q18Limit || MarginQ18 < 0
		|| MarginQ18 > FVoxelLandM5FixedSupportInternals::Q18Limit || Triangles.IsEmpty() || Triangles.Num() > 256)
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid fixed support inputs"));
	}
	const int64 Extent = RadiusQ18 + ContactSkinQ18;
	for (const FVoxelLandM5FixedPointQ18* Feet : {&FeetStart, &FeetEnd})
	{
		// All operands above are bounded by 2^38; these sums are safe in int64.
		if (Extent > FVoxelLandM5FixedSupportInternals::Q18Limit
			|| Feet->X - Extent < -FVoxelLandM5FixedSupportInternals::Q18Limit
			|| Feet->X + Extent > FVoxelLandM5FixedSupportInternals::Q18Limit
			|| Feet->Y - Extent < -FVoxelLandM5FixedSupportInternals::Q18Limit
			|| Feet->Y + Extent > FVoxelLandM5FixedSupportInternals::Q18Limit
			|| Feet->Z + HeightQ18 > FVoxelLandM5FixedSupportInternals::Q18Limit)
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Capsule footprint exceeds fixed domain"));
		}
	}
	// This covers at most 19 passes over 256 triangle records; the delegated
	// proof charges its own (quadratic) work on the same budget.
	if (!Budget->TryChargeWork(64 + int64(Triangles.Num()) * 128))
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fixed support work exhausted"));
	}
	for (const auto& Triangle : Triangles)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
		}
		if (!FVoxelLandM5FixedSupportInternals::InQ18Range(Triangle.A)
			|| !FVoxelLandM5FixedSupportInternals::InQ18Range(Triangle.B)
			|| !FVoxelLandM5FixedSupportInternals::InQ18Range(Triangle.C))
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Mesh vertex exceeds fixed domain"));
		}
		if (Triangle.RendererErrorQ18.X != 0 || Triangle.RendererErrorQ18.Y != 0 || Triangle.RendererErrorQ18.Z != 0)
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Renderer uncertainty is not supported"));
		}
	}
	const FVoxelLandM5FixedPointQ18 Origin = Triangles[0].A;
	int64 SelectedScale = 0;
	for (int32 Shift = 18; Shift >= 0; --Shift)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
		}
		const int64 Scale = int64(1) << Shift;
		if (RadiusQ18 % Scale != 0 || HeightQ18 % Scale != 0 || ContactSkinQ18 % Scale != 0 || MarginQ18 % Scale != 0
			|| RadiusQ18 / Scale > FVoxelLandM5FixedSupportInternals::ReducedLimit
			|| HeightQ18 / Scale > FVoxelLandM5FixedSupportInternals::ReducedLimit
			|| ContactSkinQ18 / Scale > FVoxelLandM5FixedSupportInternals::ReducedLimit
			|| MarginQ18 / Scale > FVoxelLandM5FixedSupportInternals::ReducedLimit)
		{
			continue;
		}
		FVoxelLandM5PointCm Temp;
		bool bFits = FVoxelLandM5FixedSupportInternals::ReducePoint(FeetStart, Origin, Scale, Temp)
				  && FVoxelLandM5FixedSupportInternals::ReducePoint(FeetEnd, Origin, Scale, Temp);
		for (const auto& T : Triangles)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5FixedSupportInternals::FixedSupportError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
			}
			bFits = bFits && FVoxelLandM5FixedSupportInternals::ReducePoint(T.A, Origin, Scale, Temp)
				 && FVoxelLandM5FixedSupportInternals::ReducePoint(T.B, Origin, Scale, Temp)
				 && FVoxelLandM5FixedSupportInternals::ReducePoint(T.C, Origin, Scale, Temp);
		}
		if (bFits)
		{
			SelectedScale = Scale;
			break;
		}
	}
	if (SelectedScale == 0)
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Lossless local fixed representation exceeds limits"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	const int64 Bytes = int64(Triangles.Num()) * int64(sizeof(FVoxelLandM5FloorTriangleCm)) * 2 + 4096;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fixed support scratch exhausted"));
	}
	TArray<FVoxelLandM5FloorTriangleCm> Reduced;
	Reduced.Reserve(Triangles.Num());
	for (const auto& T : Triangles)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
		}
		FVoxelLandM5FloorTriangleCm Entry;
		if (!FVoxelLandM5FixedSupportInternals::ReducePoint(T.A, Origin, SelectedScale, Entry.A)
			|| !FVoxelLandM5FixedSupportInternals::ReducePoint(T.B, Origin, SelectedScale, Entry.B)
			|| !FVoxelLandM5FixedSupportInternals::ReducePoint(T.C, Origin, SelectedScale, Entry.C))
		{
			return FVoxelLandM5FixedSupportInternals::FixedSupportError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("Fixed support reduction failed"));
		}
		Reduced.Add(Entry);
	}
	FVoxelLandM5PointCm Start, End;
	if (!FVoxelLandM5FixedSupportInternals::ReducePoint(FeetStart, Origin, SelectedScale, Start)
		|| !FVoxelLandM5FixedSupportInternals::ReducePoint(FeetEnd, Origin, SelectedScale, End))
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Fixed route reduction failed"));
	}
	const auto& T = Reduced[0];
	const int64 UX = T.B.X - T.A.X, UY = T.B.Y - T.A.Y, UZ = T.B.Z - T.A.Z;
	const int64 VX = T.C.X - T.A.X, VY = T.C.Y - T.A.Y, VZ = T.C.Z - T.A.Z;
	const int64 GX = -(UY * VZ - UZ * VY), GY = -(UZ * VX - UX * VZ), Den = UX * VY - UY * VX;
	if (Den <= 0)
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("First triangle is not upward oriented"));
	}
	const int64 Divisor =
		FVoxelLandM5FixedSupportInternals::GcdPositive(FVoxelLandM5FixedSupportInternals::GcdPositive(GX, GY), Den);
	if (Divisor <= 0 || GX / Divisor < -32767 || GX / Divisor > 32767 || GY / Divisor < -32767 || GY / Divisor > 32767
		|| Den / Divisor > 32767)
	{
		return FVoxelLandM5FixedSupportInternals::FixedSupportError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Exact support plane exceeds coefficient limits"));
	}
	FVoxelLandM5SupportPlane Plane;
	Plane.Origin = T.A;
	Plane.GradientX = int32(GX / Divisor);
	Plane.GradientY = int32(GY / Divisor);
	Plane.Denominator = int32(Den / Divisor);
	const FVoxelLandM5Diagnostic Contact = VoxelLand::M5::TriangleSupport::CertifyLinearContact(
		Start, End, RadiusQ18 / SelectedScale, HeightQ18 / SelectedScale, ContactSkinQ18 / SelectedScale,
		Plane, MaxSlopeNumerator, MaxSlopeDenominator, Reduced, Budget, IsCancelled);
	if (Contact.Code != EVoxelLandM5ErrorCode::None || MarginQ18 == 0)
	{
		return Contact;
	}
	return VoxelLand::M5::SupportMargin::Check(
		Start, End, RadiusQ18 / SelectedScale, MarginQ18 / SelectedScale, Plane, IsCancelled);
}
