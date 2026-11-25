#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5Composition.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5CompositionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5CompositionPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CompositionReferenceTest, "VoxelLand.M5.Numeric.Composition.Reference", M5CompositionFlags)
bool FVoxelLandM5CompositionReferenceTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Composition;
	FVoxelLandM5CavePlan Plan;
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.Id = 0; // Zero is a valid stable ID.
	Chamber.CenterCm = M5CompositionPoint(0, 0, 0);
	Chamber.RadiusCm = 100;
	Plan.Chambers.Add(Chamber);
	const auto NotCancelled = [] {
		return false;
	};
	int16 Out = -99;
	auto Sample = [&](int16 Baseline, const FVoxelLandM5PointCm& P) {
		return TryCompose(Baseline, Plan, P, 100, true, true, false, false, 0, NotCancelled, Out);
	};
	TestTrue(TEXT("sphere center succeeds"), Sample(-3000, M5CompositionPoint(0, 0, 0)));
	TestEqual(TEXT("100 cm cave air converts to one voxel"), Out, int16(1024));
	TestTrue(TEXT("surface succeeds"), Sample(-3000, M5CompositionPoint(100, 0, 0)));
	TestEqual(TEXT("primitive surface is quantized zero"), Out, int16(0));
	TestTrue(TEXT("outside primitive succeeds"), Sample(-3000, M5CompositionPoint(150, 0, 0)));
	TestEqual(TEXT("hard subtraction influences solid outside radius"), Out, int16(-512));
	TestTrue(TEXT("air remains air"), Sample(900, M5CompositionPoint(150, 0, 0)));
	TestEqual(TEXT("air not filled"), Out, int16(900));

	FVoxelLandM5Segment Segment;
	Segment.bHasId = true;
	Segment.bHasConnectionId = true;
	Segment.StartCm = M5CompositionPoint(200, 0, 0);
	Segment.EndCm = M5CompositionPoint(400, 0, 0);
	Segment.RadiusCm = 50;
	Plan.Segments.Add(Segment);
	TestTrue(TEXT("segment midpoint succeeds"), Sample(-3000, M5CompositionPoint(300, 0, 0)));
	TestEqual(TEXT("segment carving union"), Out, int16(512));
	Out = 777;
	Plan.Segments[0].Profile.Version = 2;
	TestFalse(TEXT("mixed primitive version rejected"), Sample(-3000, M5CompositionPoint(300, 0, 0)));
	TestEqual(TEXT("mixed version leaves output"), Out, int16(777));
	Plan.GeometryVersion = 2;
	Plan.Chambers[0].Profile.Version = 2;
	TestFalse(TEXT("unbound geometry version rejected"), Sample(-3000, M5CompositionPoint(300, 0, 0)));
	TestEqual(TEXT("unsupported geometry leaves output"), Out, int16(777));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CompositionBoundariesTest, "VoxelLand.M5.Numeric.Composition.Boundaries", M5CompositionFlags)
bool FVoxelLandM5CompositionBoundariesTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Composition;
	FVoxelLandM5CavePlan Plan;
	const FVoxelLandM5PointCm Origin = M5CompositionPoint(0, 0, 0);
	const auto NotCancelled = [] {
		return false;
	};
	int16 Out = 777;
	auto Pocket = [&](int16 Baseline, int32 Size, int64 Distance) {
		return TryCompose(Baseline, Plan, Origin, Size, true, true, false, true, Distance, NotCancelled, Out);
	};
	TestTrue(TEXT("positive half succeeds"), Pocket(-300, 200, -25));
	TestEqual(TEXT("positive tie rounds away from zero"), Out, int16(1));
	TestTrue(TEXT("negative half succeeds"), Pocket(-300, 200, 25));
	TestEqual(TEXT("negative tie rounds away from zero"), Out, int16(-1));
	TestTrue(TEXT("below positive half succeeds"), Pocket(-300, 200, -24));
	TestEqual(TEXT("below half rounds to zero"), Out, int16(0));
	TestTrue(TEXT("positive saturation succeeds"), Pocket(-300, 1, -1000000));
	TestEqual(TEXT("positive saturation"), Out, int16(MAX_int16));
	TestTrue(TEXT("negative saturation succeeds"), Pocket(MIN_int16, 1, 1000000));
	TestEqual(TEXT("negative saturation"), Out, int16(MIN_int16));

	TestTrue(TEXT("disabled ignores unused invalid settings"),
		TryCompose(-17, Plan, Origin, 0, false, true, false, true, MIN_int64, NotCancelled, Out));
	TestEqual(TEXT("disabled returns original"), Out, int16(-17));
	TestTrue(TEXT("protected overrides carve"),
		TryCompose(-17, Plan, Origin, 100, true, true, true, true, -1000000, NotCancelled, Out));
	TestEqual(TEXT("protected returns original"), Out, int16(-17));
	TestTrue(TEXT("disallowed returns original"),
		TryCompose(-18, Plan, Origin, 100, true, false, false, true, -1000000, NotCancelled, Out));
	TestEqual(TEXT("disallowed unchanged"), Out, int16(-18));

	Out = 777;
	TestFalse(TEXT("zero voxel size"), Pocket(-300, 0, 0));
	TestEqual(TEXT("invalid size is atomic"), Out, int16(777));
	TestFalse(TEXT("negation overflow"), Pocket(-300, 100, MIN_int64));
	TestEqual(TEXT("overflow is atomic"), Out, int16(777));
	TestFalse(TEXT("multiplication overflow"), Pocket(-300, 100, MAX_int64));
	TestEqual(TEXT("multiply overflow is atomic"), Out, int16(777));
	TestFalse(TEXT("position outside numeric domain"),
		TryCompose(-300, Plan, M5CompositionPoint(1048577, 0, 0), 100, true, true, false, false, 0, NotCancelled, Out));
	TestEqual(TEXT("out-of-domain is atomic"), Out, int16(777));
	FVoxelLandM5Chamber Invalid;
	Invalid.bHasId = true;
	Invalid.RadiusCm = 0;
	Plan.Chambers.Add(Invalid);
	TestFalse(TEXT("invalid radius rejected"), Pocket(-300, 100, 0));
	TestEqual(TEXT("invalid plan is atomic"), Out, int16(777));
	Plan.Chambers[0].RadiusCm = 1;
	Plan.Chambers[0].bHasId = false;
	TestFalse(TEXT("missing ID rejected"), Pocket(-300, 100, 0));
	TestEqual(TEXT("missing ID is atomic"), Out, int16(777));
	Plan.Chambers[0].bHasId = true;
	int32 Checks = 0;
	auto CancelAfterStart = [&Checks] {
		return ++Checks == 2;
	};
	TestFalse(TEXT("cancel while evaluating primitive"),
		TryCompose(-300, Plan, Origin, 100, true, true, false, false, 0, CancelAfterStart, Out));
	TestEqual(TEXT("cancellation is atomic"), Out, int16(777));
	return true;
}
#endif
