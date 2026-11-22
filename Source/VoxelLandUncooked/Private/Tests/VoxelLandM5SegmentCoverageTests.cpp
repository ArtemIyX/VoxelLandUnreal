#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5SegmentCoverage.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SegmentCoverageFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5CoveragePoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SegmentCoverageTest, "VoxelLand.M5.Routes.SegmentCoverage", M5SegmentCoverageFlags)
bool FVoxelLandM5SegmentCoverageTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create budget"), FVoxelLandM5Budget::TryCreate(0, 1000, Budget)) || !Budget.IsValid())
	{
		return false;
	}
	FVoxelLandM5Segment Segment;
	Segment.bHasId = true;
	Segment.Id = 0; // Zero is a valid ID.
	Segment.StartCm = M5CoveragePoint(-200, -20, 300);
	Segment.EndCm = M5CoveragePoint(0, -20, 400);
	Segment.RadiusCm = 103;
	FVoxelLandM5CapsuleProfile Capsule;
	FVoxelLandM5RegionCoord Region;
	Region.X = -2;
	Region.Y = 3;
	FVoxelLandM5RouteLeg Leg;
	Leg.Feature.bValid = true;
	Leg.Feature.Kind = EVoxelLandM5RouteFeatureKind::Segment;
	Leg.Feature.Id = 0;
	Leg.Feature.Region = Region;
	// Independent analytic reference: standing height 185 cm puts the feet
	// 92 cm below each center; the inflated enclosing-ball radius is 103 cm.
	Leg.StartFeetCm = M5CoveragePoint(-200, -20, 208);
	Leg.EndFeetCm = M5CoveragePoint(0, -20, 308);
	const auto NeverCancel = [] {
		return false;
	};
	const auto Check = [&](EVoxelLandM5ErrorCode Expected) {
		return TestTrue(TEXT("expected coverage result"),
			VoxelLand::M5::SegmentCoverage::Check(Segment, Leg, Region, Capsule, Budget.ToSharedRef(), NeverCancel).Code
				== Expected);
	};
	Check(EVoxelLandM5ErrorCode::None);
	Swap(Leg.StartFeetCm, Leg.EndFeetCm);
	Check(EVoxelLandM5ErrorCode::None);
	Swap(Leg.StartFeetCm, Leg.EndFeetCm);
	Leg.StartFeetCm.X = -199; // A partially traversed segment is not full coverage.
	Check(EVoxelLandM5ErrorCode::Unproven);
	Leg.StartFeetCm.X = 10000; // A correct label cannot certify a remote leg.
	Check(EVoxelLandM5ErrorCode::Unproven);
	Leg.StartFeetCm.X = -200;
	Leg.Feature.Region.X = -1;
	Check(EVoxelLandM5ErrorCode::InconsistentTopology);
	Leg.Feature.Region = Region;
	Leg.Feature.Id = 1;
	Check(EVoxelLandM5ErrorCode::InconsistentTopology);
	Leg.Feature.Id = 0;
	Leg.Feature.Kind = EVoxelLandM5RouteFeatureKind::Connection;
	Check(EVoxelLandM5ErrorCode::InconsistentTopology);
	Leg.Feature.Kind = EVoxelLandM5RouteFeatureKind::Segment;
	Leg.Feature.bValid = false;
	Check(EVoxelLandM5ErrorCode::InconsistentTopology);
	Leg.Feature.bValid = true;
	Leg.Transition = EVoxelLandM5RouteTransition::Step;
	Check(EVoxelLandM5ErrorCode::Unproven);
	Leg.Transition = EVoxelLandM5RouteTransition::Walk;
	Segment.RadiusCm = 102;
	Check(EVoxelLandM5ErrorCode::Unproven);
	Segment.RadiusCm = 103;
	const auto Cancelled = [] {
		return true;
	};
	TestTrue(TEXT("cancellation is explicit"),
		VoxelLand::M5::SegmentCoverage::Check(Segment, Leg, Region, Capsule, Budget.ToSharedRef(), Cancelled).Code
			== EVoxelLandM5ErrorCode::Cancelled);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TooSmall;
	if (!TestTrue(TEXT("small budget created"), FVoxelLandM5Budget::TryCreate(0, 39, TooSmall)) || !TooSmall.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("proposal work budget enforced"),
		VoxelLand::M5::SegmentCoverage::Check(Segment, Leg, Region, Capsule, TooSmall.ToSharedRef(), NeverCancel).Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	return true;
}
#endif
