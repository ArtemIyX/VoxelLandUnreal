#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFeetProposalBuilder.h"
#include "Generation/Caves/Geometry/VoxelLandCaveProfileContainment.h"
#include "Generation/Caves/Geometry/VoxelLandCaveSegmentCoverage.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProfileContainmentTest, "VoxelLand.M5.ProfileContainment.Ideal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ProfileContainmentTest::RunTest(const FString& Parameters)
{
	using E = EVoxelLandM5ErrorCode;
	FVoxelLandM5Segment S;
	S.bHasId = S.bHasConnectionId = true;
	S.Id = 7;
	S.EndCm.X = 1000;
	S.RadiusCm = 45;
	S.Profile.Version = 2;
	S.Profile.StartFloorCm = S.Profile.EndFloorCm = 0;
	S.Profile.StartCeilingCm = S.Profile.EndCeilingCm = 300;
	FVoxelLandM5CapsuleProfile C;
	FVoxelLandM5FeetProposal Feet;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(0, 10000, Budget))
	{
		return false;
	}
	const auto Never = [] {
		return false;
	};
	auto Code = VoxelLand::M5::FeetProposalBuilder::Build(S, C, Budget.ToSharedRef(), Never, Feet);
	TestEqual(TEXT("flat profile proposes"), Code.Code, E::None);
	TestEqual(TEXT("inflated lower envelope touches floor"), Feet.StartFeetCm.Z, int64(10));
	TestEqual(TEXT("flat final foot"), Feet.EndFeetCm.Z, int64(10));
	TestEqual(TEXT("flat ideal envelope"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Feet.StartFeetCm, Feet.EndFeetCm), E::None);
	S.RadiusCm = 44;
	TestEqual(TEXT("one centimetre narrow rejects"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Feet.StartFeetCm, Feet.EndFeetCm), E::Unproven);
	S.RadiusCm = 45;
	FVoxelLandM5PointCm Below = Feet.StartFeetCm;
	Below.Z = 9;
	TestEqual(TEXT("one centimetre floor intrusion rejects"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Below, Feet.EndFeetCm), E::Unproven);
	S.Profile.EndFloorCm = 100;
	S.Profile.EndCeilingCm = 400;
	Code = VoxelLand::M5::FeetProposalBuilder::Build(S, C, Budget.ToSharedRef(), Never, Feet);
	TestEqual(TEXT("ramp profile proposes"), Code.Code, E::None);
	// Independent conservative bound: ceil(2*45*100/1000)+1 = 10 cm.
	TestEqual(TEXT("start includes slope envelope"), Feet.StartFeetCm.Z, int64(20));
	TestEqual(TEXT("end includes slope envelope"), Feet.EndFeetCm.Z, int64(120));
	TestEqual(TEXT("reverse sweep has same proof"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Feet.EndFeetCm, Feet.StartFeetCm), E::None);
	Below = Feet.StartFeetCm;
	Below.Z--;
	TestEqual(TEXT("one below slope envelope rejects"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Below, Feet.EndFeetCm), E::Unproven);
	FVoxelLandM5RouteLeg Leg;
	Leg.Feature.bValid = true;
	Leg.Feature.Kind = EVoxelLandM5RouteFeatureKind::Segment;
	Leg.Feature.Id = 7;
	Leg.StartFeetCm = Feet.StartFeetCm;
	Leg.EndFeetCm = Feet.EndFeetCm;
	FVoxelLandM5RegionCoord Region;
	Code = VoxelLand::M5::SegmentCoverage::Check(S, Leg, Region, C, Budget.ToSharedRef(), Never);
	TestEqual(TEXT("segment dispatch checks profile"), Code.Code, E::None);
	Leg.Transition = EVoxelLandM5RouteTransition::Step;
	Code = VoxelLand::M5::SegmentCoverage::Check(S, Leg, Region, C, Budget.ToSharedRef(), Never);
	TestEqual(TEXT("unproved step rejected"), Code.Code, E::Unproven);
	S.EndCm.X = 0;
	TestEqual(TEXT("vertical profile rejected"),
		VoxelLand::M5::ProfileContainment::Check(S, C, Feet.StartFeetCm, Feet.EndFeetCm), E::Unproven);
	S.EndCm.X = 1000;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
	if (!FVoxelLandM5Budget::TryCreate(0, 63, Tiny))
	{
		return false;
	}
	Feet.StartFeetCm.Z = 777;
	Code = VoxelLand::M5::FeetProposalBuilder::Build(S, C, Tiny.ToSharedRef(), Never, Feet);
	TestEqual(TEXT("work exhaustion rejected"), Code.Code, E::BudgetExceeded);
	TestEqual(TEXT("failure preserves output"), Feet.StartFeetCm.Z, int64(777));
	int32 Polls = 0;
	Code =
		VoxelLand::M5::FeetProposalBuilder::Build(S, C, Budget.ToSharedRef(), [&Polls] { return ++Polls == 2; }, Feet);
	TestEqual(TEXT("final cancellation"), Code.Code, E::Cancelled);
	TestEqual(TEXT("cancellation preserves output"), Feet.StartFeetCm.Z, int64(777));
	return true;
}
#endif
