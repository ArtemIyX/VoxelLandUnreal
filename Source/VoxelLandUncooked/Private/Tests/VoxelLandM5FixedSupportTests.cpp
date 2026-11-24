#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5FixedSupport.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5FixedSupportFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
constexpr int64 QuarterCmQ18 = int64(1) << 16;

FVoxelLandM5FixedPointQ18 FixedSupportPoint(int64 X, int64 Y, int64 Z)
{
	// Translation is deliberately nonzero; quarter-cm positions lie exactly
	// on the M4 100-cm voxel snap grid (1/4096 cm).
	FVoxelLandM5FixedPointQ18 P;
	P.X = (X + 400) * QuarterCmQ18;
	P.Y = (Y - 800) * QuarterCmQ18;
	P.Z = (Z + 1200) * QuarterCmQ18;
	return P;
}

TArray<FVoxelLandM5FixedTriangleQ18> FixedSupportFan()
{
	// Local exact plane z=3x/4; four independently specified faces.
	const FVoxelLandM5FixedPointQ18 Ring[] = {FixedSupportPoint(-20, -20, -15), FixedSupportPoint(20, -20, 15),
		FixedSupportPoint(20, 20, 15), FixedSupportPoint(-20, 20, -15)};
	const auto Center = FixedSupportPoint(0, 0, 0);
	TArray<FVoxelLandM5FixedTriangleQ18> Result;
	for (int32 I = 0; I < 4; ++I)
	{
		FVoxelLandM5FixedTriangleQ18 T;
		T.A = Ring[I];
		T.B = Ring[(I + 1) % 4];
		T.C = Center;
		Result.Add(T);
	}
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FixedSupportAnalyticTest, "VoxelLand.M5.FixedSupport.Analytic", M5FixedSupportFlags)
bool FVoxelLandM5FixedSupportAnalyticTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::FixedSupport::CertifyLinearContact;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1000000, 10000000, Budget)))
	{
		return false;
	}
	const auto NeverCancel = [] {
		return false;
	};
	const auto Start = FixedSupportPoint(1, 0, 2), End = FixedSupportPoint(5, 0, 5);
	TArray<FVoxelLandM5FixedTriangleQ18> Triangles = FixedSupportFan();
	const auto Check = [&](int64 SkinQ18, int32 SlopeN, int32 SlopeD) {
		return CertifyLinearContact(Start, End, 5 * QuarterCmQ18, 185 * QuarterCmQ18, SkinQ18, SlopeN, SlopeD,
			Triangles, Budget.ToSharedRef(), NeverCancel)
			.Code;
	};
	// In local quarter-centimetre units the plane normal is (-3,0,4).
	// Both lower-sphere centers have dot=25 and normal length=5, so the
	// exact support distance is five units: equal to the capsule radius.
	TestEqual(TEXT("fractional snapped coplanar fan"), int32(Check(0, 3, 4)), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("too steep"), int32(Check(0, 1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	// Plane normal (-3,0,4). In sixteenth-quarter units the center gap
	// numerator is 400; reach 81 gives rise 243, so 157^2+320^2 > 324^2.
	using VoxelLand::M5::FixedSupport::CertifyLinearContactWithMargin;
	TestEqual(TEXT("sloped lower-cap margin"), int32(CertifyLinearContactWithMargin(Start, End,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, 0, QuarterCmQ18 / 16, 3, 4, Triangles,
		Budget.ToSharedRef(), NeverCancel).Code), int32(EVoxelLandM5ErrorCode::None));
	const FVoxelLandM5Diagnostic OutsideFootprint = CertifyLinearContactWithMargin(Start, End,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, 0, QuarterCmQ18, 3, 4, Triangles,
		Budget.ToSharedRef(), NeverCancel);
	TestTrue(TEXT("lower cap outside nominal footprint rejected"),
		OutsideFootprint.Code == EVoxelLandM5ErrorCode::Unproven
		&& OutsideFootprint.Message == TEXT("Support margin exceeds nominal footprint"));
	// Radius 4, margin 1, plane numerator 27 and rise 15 give a 12-16-20
	// exact equality; a tangent at nominal footprint boundary is unproven.
	const FVoxelLandM5FixedPointQ18 FootprintStart = FixedSupportPoint(-1, 0, 2);
	const FVoxelLandM5FixedPointQ18 FootprintEnd = FixedSupportPoint(3, 0, 5);
	const FVoxelLandM5Diagnostic FootprintBoundary = CertifyLinearContactWithMargin(FootprintStart, FootprintEnd,
		4 * QuarterCmQ18, 185 * QuarterCmQ18, 2 * QuarterCmQ18, QuarterCmQ18, 3, 4,
		Triangles, Budget.ToSharedRef(), NeverCancel);
	TestTrue(TEXT("footprint equality rejected"), FootprintBoundary.Code == EVoxelLandM5ErrorCode::Unproven
		&& FootprintBoundary.Message == TEXT("Support margin exceeds nominal footprint"));
	// A separate supported path has numerator 27: margin 4 gives exact
	// equality 3*(5+4)=27; margin 5 rises strictly into the side region.
	const FVoxelLandM5FixedPointQ18 BoundaryStart = FixedSupportPoint(-1, 0, 1);
	const FVoxelLandM5FixedPointQ18 BoundaryEnd = FixedSupportPoint(3, 0, 4);
	TestEqual(TEXT("strict boundary rejected"), int32(CertifyLinearContactWithMargin(BoundaryStart, BoundaryEnd,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, QuarterCmQ18, 4 * QuarterCmQ18, 3, 4,
		Triangles, Budget.ToSharedRef(), NeverCancel).Code), int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("support plane reaches side"), int32(CertifyLinearContactWithMargin(BoundaryStart, BoundaryEnd,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, QuarterCmQ18, 5 * QuarterCmQ18, 3, 4,
		Triangles, Budget.ToSharedRef(), NeverCancel).Code), int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("oversized margin fails closed"), int32(CertifyLinearContactWithMargin(Start, End,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, 0, MAX_int64, 3, 4, Triangles,
		Budget.ToSharedRef(), NeverCancel).Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("margin cancellation"), int32(CertifyLinearContactWithMargin(Start, End,
		5 * QuarterCmQ18, 185 * QuarterCmQ18, 0, QuarterCmQ18, 3, 4, Triangles,
		Budget.ToSharedRef(), [] { return true; }).Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	Triangles[0].RendererErrorQ18.X = 1;
	TestEqual(
		TEXT("unknown renderer error is not ignored"), int32(Check(0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles[0].RendererErrorQ18.X = 0;
	Triangles.RemoveAt(1);
	TestEqual(TEXT("missing floor remains unproven"), int32(Check(0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles = FixedSupportFan();
	++Triangles[0].A.X;
	TestEqual(TEXT("unrepresentable fine-grid patch fails closed"), int32(Check(0, 3, 4)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	const auto Cancelled = [] {
		return true;
	};
	TestEqual(TEXT("cancellation"),
		int32(CertifyLinearContact(
			Start, End, 5 * QuarterCmQ18, 185 * QuarterCmQ18, 0, 3, 4, Triangles, Budget.ToSharedRef(), Cancelled)
				.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}
#endif
