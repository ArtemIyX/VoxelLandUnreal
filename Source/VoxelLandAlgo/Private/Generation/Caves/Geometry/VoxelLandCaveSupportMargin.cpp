#include "VoxelLandCaveSupportMargin.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

class FVoxelLandM5SupportMarginInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FixedSupport");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::SupportMargin::Check(const FVoxelLandM5PointCm& Start,
	const FVoxelLandM5PointCm& End, int64 Radius, int64 Margin, const FVoxelLandM5SupportPlane& Plane,
	TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	using I = FVoxelLandM5SupportMarginInternals;
	int64 Reach = 0, Gradient = 0, Rise = 0;
	if (!TryAdd(Radius, Margin, Reach)
		|| !TryAdd(FMath::Abs(int64(Plane.GradientX)), FMath::Abs(int64(Plane.GradientY)), Gradient)
		|| !TryMultiplyNonNegative(Reach, Gradient, Rise))
	{
		return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Support margin bound overflow"));
	}
	int64 NominalRadial = 0, InflatedRadial = 0, NominalSquared = 0, InflatedSquared = 0;
	if (!TryMultiplyNonNegative(Radius, Plane.Denominator, NominalRadial)
		|| !TryMultiplyNonNegative(Reach, Plane.Denominator, InflatedRadial)
		|| !TryMultiplyNonNegative(NominalRadial, NominalRadial, NominalSquared)
		|| !TryMultiplyNonNegative(InflatedRadial, InflatedRadial, InflatedSquared))
	{
		return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Support footprint bound overflow"));
	}
	for (const FVoxelLandM5PointCm* Feet : {&Start, &End})
	{
		if (IsCancelled())
		{
			return I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"));
		}
		int64 Center = 0, DeltaZ = 0, DeltaX = 0, DeltaY = 0;
		int64 ZTerm = 0, XTerm = 0, YTerm = 0, Gap = 0;
		if (!TryAdd(Feet->Z, Radius, Center) || !TrySubtract(Center, Plane.Origin.Z, DeltaZ)
			|| !TrySubtract(Feet->X, Plane.Origin.X, DeltaX) || !TrySubtract(Feet->Y, Plane.Origin.Y, DeltaY)
			|| !TryMultiply(DeltaZ, Plane.Denominator, ZTerm) || !TryMultiply(DeltaX, Plane.GradientX, XTerm)
			|| !TryMultiply(DeltaY, Plane.GradientY, YTerm) || !TrySubtract(ZTerm, XTerm, Gap)
			|| !TrySubtract(Gap, YTerm, Gap) || Gap <= Rise)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Support margin reaches capsule side"));
		}
		int64 VerticalGap = 0, VerticalSquared = 0, DistanceSquared = 0;
		if (!TrySubtract(Gap, Rise, VerticalGap) || !TryMultiplyNonNegative(VerticalGap, VerticalGap, VerticalSquared)
			|| !TryAdd(VerticalSquared, NominalSquared, DistanceSquared) || DistanceSquared <= InflatedSquared)
		{
			return I::Error(EVoxelLandM5ErrorCode::Unproven, TEXT("Support margin exceeds nominal footprint"));
		}
	}
	return IsCancelled() ? I::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Fixed support cancelled"))
						 : FVoxelLandM5Diagnostic{};
}
