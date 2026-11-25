#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFeetProposalBuilder.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5FeetProposalFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5FeetTestPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Result;
	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FeetProposalTest, "VoxelLand.M5.Routes.IdealSegmentProposal", M5FeetProposalFlags)
bool FVoxelLandM5FeetProposalTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(0, 10000, Budget)) || !Budget.IsValid())
	{
		return false;
	}
	FVoxelLandM5Segment Segment;
	Segment.bHasId = true;
	Segment.Id = 0; // Zero is a valid feature ID.
	Segment.StartCm = M5FeetTestPoint(-200, 3, 400);
	Segment.EndCm = M5FeetTestPoint(0, 3, 500);
	Segment.RadiusCm = 103;
	FVoxelLandM5CapsuleProfile Capsule;
	FVoxelLandM5FeetProposal Output;
	const auto NeverCancel = [] {
		return false;
	};
	const auto Check = [&](EVoxelLandM5ErrorCode Expected) {
		return TestTrue(TEXT("expected result"),
			VoxelLand::M5::FeetProposalBuilder::Build(Segment, Capsule, Budget.ToSharedRef(), NeverCancel, Output).Code
				== Expected);
	};
	if (!Check(EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	// 185 cm full height => feet offset 92 cm. The furthest capsule point
	// lies 93 cm from the ideal centerline; 10 cm margin makes 103 cm.
	TestEqual(TEXT("negative X retained"), Output.StartFeetCm.X, int64(-200));
	TestEqual(TEXT("odd-height start"), Output.StartFeetCm.Z, int64(308));
	TestEqual(TEXT("odd-height end"), Output.EndFeetCm.Z, int64(408));

	// A diagonal XY path at the conservative 45-degree slope boundary:
	// 90 * 100 == 45 * max(200, 200). At t=0, 1/2 and 1 the expected
	// line centers are respectively (-100,-100,0), (0,0,50), (100,100,100).
	// The lower/upper capsule medial endpoints are 57/58 cm from those
	// centers, and each end-cap radius including margin is 35+10=45 cm.
	// Thus 57+45=102 and 58+45=103 both fit the 103 cm swept-ball tube.
	Segment.StartCm = M5FeetTestPoint(-100, -100, 0);
	Segment.EndCm = M5FeetTestPoint(100, 100, 100);
	if (!Check(EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("oblique start X"), Output.StartFeetCm.X, int64(-100));
	TestEqual(TEXT("oblique start Y"), Output.StartFeetCm.Y, int64(-100));
	TestEqual(TEXT("oblique start feet Z"), Output.StartFeetCm.Z, int64(-92));
	TestEqual(TEXT("oblique end X"), Output.EndFeetCm.X, int64(100));
	TestEqual(TEXT("oblique end Y"), Output.EndFeetCm.Y, int64(100));
	TestEqual(TEXT("oblique end feet Z"), Output.EndFeetCm.Z, int64(8));
	for (int32 T = 0; T <= 2; ++T)
	{
		const int64 CenterZ = int64(T) * 50;
		const int64 FeetZ = Output.StartFeetCm.Z + int64(T) * 50;
		const int64 BottomMedialZ = FeetZ + Capsule.RadiusCm;
		const int64 TopMedialZ = FeetZ + Capsule.StandingHeightCm - Capsule.RadiusCm;
		TestEqual(TEXT("oblique lower medial offset"), CenterZ - BottomMedialZ, int64(57));
		TestEqual(TEXT("oblique upper medial offset"), TopMedialZ - CenterZ, int64(58));
		TestTrue(TEXT("oblique lower inflated end cap contained"), CenterZ - BottomMedialZ + 45 <= Segment.RadiusCm);
		TestTrue(TEXT("oblique upper inflated end cap contained"), TopMedialZ - CenterZ + 45 <= Segment.RadiusCm);
	}
	Segment.EndCm.Z = 101; // One centimetre past the conservative slope bound.
	Output.StartFeetCm.X = 771;
	Check(EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("oblique slope failure leaves output"), Output.StartFeetCm.X, int64(771));

	Segment.StartCm = M5FeetTestPoint(-200, 3, 400);
	Segment.EndCm = M5FeetTestPoint(0, 3, 500);
	Segment.RadiusCm = 102;
	Output.StartFeetCm.X = 771;
	Check(EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("failed clearance leaves output"), Output.StartFeetCm.X, int64(771));
	Segment.RadiusCm = 103;
	Segment.EndCm.Z = 501; // 90*101 exceeds 45*200.
	Check(EVoxelLandM5ErrorCode::Unproven);
	Segment.EndCm = Segment.StartCm;
	Check(EVoxelLandM5ErrorCode::InvalidInput);
	Segment.EndCm = M5FeetTestPoint(1048577, 3, 400);
	Check(EVoxelLandM5ErrorCode::OutOfDomain);
	Segment.EndCm = M5FeetTestPoint(1048576, 3, 400);
	Check(EVoxelLandM5ErrorCode::OutOfDomain); // Capsule extends beyond endpoint.
	Segment.EndCm = M5FeetTestPoint(0, 3, 400);
	Capsule.StandingHeightCm = 69; // Diameter is 70 cm.
	Check(EVoxelLandM5ErrorCode::InvalidInput);
	Capsule.StandingHeightCm = 185;
	const auto Cancel = [] {
		return true;
	};
	TestTrue(TEXT("cancelled"),
		VoxelLand::M5::FeetProposalBuilder::Build(Segment, Capsule, Budget.ToSharedRef(), Cancel, Output).Code
			== EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancel leaves output"), Output.StartFeetCm.X, int64(771));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (!TestTrue(TEXT("empty work budget created"), FVoxelLandM5Budget::TryCreate(0, 31, EmptyBudget))
		|| !EmptyBudget.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("work limit enforced"),
		VoxelLand::M5::FeetProposalBuilder::Build(Segment, Capsule, EmptyBudget.ToSharedRef(), NeverCancel, Output).Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("budget failure leaves output"), Output.StartFeetCm.X, int64(771));
	return true;
}
#endif
