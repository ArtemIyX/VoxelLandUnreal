#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5PocketNoise.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5PocketFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PocketVectorsTest, "VoxelLand.M5.Pockets.ReferenceVectors", M5PocketFlags)
bool FVoxelLandM5PocketVectorsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::PocketNoise;
	const FGuid Stage(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	constexpr uint64 Salt = 0x0102030405060708ULL;
	TArray<uint8> Packet;
	TestTrue(TEXT("corner packet"), TryCornerPacket(-1, Stage, Salt, -1, 2, -3, Packet));
	TestEqual(TEXT("independent packet"), BytesToHex(Packet.GetData(), Packet.Num()).ToLower(),
		FString(TEXT(
			"564c4d3550434b0001000000ffffffffffffffff67452301efcdab894030201098badcfe0807060504030201ffffffffffffffff0200000000000000fdffffffffffffff")));
	const auto NotCancelled = []() {
		return false;
	};
	int32 Value = 0;
	TestTrue(TEXT("origin sample"), TrySample(0, 0, 0, 16, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("origin reference"), Value, 24801);
	TestTrue(TEXT("midpoint sample"), TrySample(8, 8, 8, 16, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("midpoint reference"), Value, 11510);
	TestTrue(TEXT("negative crossing sample"), TrySample(-1, -17, 25, 16, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("negative crossing reference"), Value, 11499);
	TestTrue(TEXT("supported coordinate extremes"),
		TrySample(1048576, -1048576, 0, 1024, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("extreme coordinate reference"), Value, -16330);
	TestTrue(TEXT("before lattice plane"), TrySample(15, 0, 0, 16, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("before plane reference"), Value, 28032);
	TestTrue(TEXT("on lattice plane"), TrySample(16, 0, 0, 16, -1, Stage, Salt, NotCancelled, Value));
	TestEqual(TEXT("on plane reference"), Value, 28248);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5PocketFailureTest, "VoxelLand.M5.Pockets.FailuresAndRepeatability", M5PocketFlags)
bool FVoxelLandM5PocketFailureTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::PocketNoise;
	const auto NotCancelled = []() {
		return false;
	};
	const FGuid Stage(1, 2, 3, 4);
	int32 Value = 424242;
	TestFalse(TEXT("zero period"), TrySample(0, 0, 0, 0, 0, Stage, 0, NotCancelled, Value));
	TestFalse(TEXT("negative period"), TrySample(0, 0, 0, -1, 0, Stage, 0, NotCancelled, Value));
	TestFalse(TEXT("excess period"), TrySample(0, 0, 0, 1048577, 0, Stage, 0, NotCancelled, Value));
	TestFalse(TEXT("x beyond range"), TrySample(1048577, 0, 0, 16, 0, Stage, 0, NotCancelled, Value));
	TestFalse(TEXT("y beyond range"), TrySample(0, -1048577, 0, 16, 0, Stage, 0, NotCancelled, Value));
	TestFalse(TEXT("z extreme"), TrySample(0, 0, MIN_int64, 16, 0, Stage, 0, NotCancelled, Value));
	TestEqual(TEXT("invalid input preserves output"), Value, 424242);
	int32 Polls = 0;
	const auto CancelMiddle = [&Polls]() {
		return ++Polls == 5;
	};
	TestFalse(TEXT("cancel after four corners"), TrySample(1, -1, 2, 16, 0, Stage, 0, CancelMiddle, Value));
	TestEqual(TEXT("cancel mid-sample preserves output"), Value, 424242);
	Polls = 0;
	const auto CancelBeforePublish = [&Polls]() {
		return ++Polls == 9;
	};
	TestFalse(TEXT("cancel immediately before publication"),
		TrySample(1, -1, 2, 16, 0, Stage, 0, CancelBeforePublish, Value));
	TestEqual(TEXT("late cancellation preserves output"), Value, 424242);

	// Repeated calls at representative global coordinates establish scalar
	// repeatability only. Actual chunk-overlap seams need integration tests.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int64 Boundary : {-32LL, 0LL, 32LL})
		{
			int64 Position[3] = {-7, 13, -19};
			Position[Axis] = Boundary;
			int32 Left = 0;
			int32 Right = 0;
			TestTrue(TEXT("first owner sample"),
				TrySample(Position[0], Position[1], Position[2], 17, 5, Stage, 6, NotCancelled, Left));
			TestTrue(TEXT("neighbor sample"),
				TrySample(Position[0], Position[1], Position[2], 17, 5, Stage, 6, NotCancelled, Right));
			TestEqual(TEXT("repeated global sample"), Left, Right);
			TestTrue(TEXT("value in bounded range"), Left >= -32768 && Left <= 32767);
		}
	}
	return true;
}
#endif
