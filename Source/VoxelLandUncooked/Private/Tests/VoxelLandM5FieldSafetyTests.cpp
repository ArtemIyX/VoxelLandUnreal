#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5FieldSafety.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5FieldSafetyFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FieldSafetyProtectionTest, "VoxelLand.M5.FieldSafety.Protection", M5FieldSafetyFlags)
bool FVoxelLandM5FieldSafetyProtectionTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::FieldSafety;
	const auto NeverCancel = []() {
		return false;
	};
	// Two adjacent X cells, whose padded sample grid is 5 by 4 by 4.
	TArray<int16> Baseline;
	Baseline.Init(-4, 80);
	TArray<int16> Final = Baseline;
	TArray<uint8> Protection = {0, 1};
	TArray<uint8> Roof = {1, 0};
	TArray<uint8> Entrance = {1, 0};
	const auto Check = [&]() {
		return CheckUnchangedProtectedCells(
			FIntVector(2, 1, 1), Baseline, Final, Protection, Roof, Entrance, 128, NeverCancel);
	};
	TestEqual(TEXT("unchanged field succeeds"), int32(Check().Code), int32(EVoxelLandM5ErrorCode::None));
	// All protected-cell samples become air: this must fail even if a mesher
	// would produce no triangles within the protected volume.
	Final.Init(4, 80);
	TestEqual(
		TEXT("fully carved protected volume rejected"), int32(Check().Code), int32(EVoxelLandM5ErrorCode::Blocked));
	Final = Baseline;
	// x=4 is the far halo of the second cell (indices 1..4). It is not
	// a corner of that cell; it can still affect an adjacent owned mesh cell.
	Final[4] = 5;
	TestEqual(TEXT("protected cell halo is checked"), int32(Check().Code), int32(EVoxelLandM5ErrorCode::Blocked));
	Final = Baseline;
	// The first cell is an authorized roof entrance, and x=0 belongs only to its
	// 4-sample neighborhood, not to that of the protected neighboring cell.
	Final[0] = 5;
	TestEqual(TEXT("entrance exemption does not affect neighboring protected cell"), int32(Check().Code),
		int32(EVoxelLandM5ErrorCode::None));
	Protection[0] = 1;
	TestEqual(
		TEXT("explicit protection overrides entrance"), int32(Check().Code), int32(EVoxelLandM5ErrorCode::Blocked));
	Protection[0] = 0;
	Entrance[0] = 0;
	TestEqual(TEXT("non-entrance roof protected"), int32(Check().Code), int32(EVoxelLandM5ErrorCode::Blocked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FieldSafetyFailureTest, "VoxelLand.M5.FieldSafety.Failures", M5FieldSafetyFlags)
bool FVoxelLandM5FieldSafetyFailureTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::FieldSafety;
	const auto NeverCancel = []() {
		return false;
	};
	const auto AlwaysCancel = []() {
		return true;
	};
	TArray<int16> Samples;
	Samples.Init(-1, 64);
	TArray<uint8> Mask = {1};
	TArray<uint8> NoMask = {0};
	const auto Check = [&](FIntVector Size, TConstArrayView<int16> Final, int64 Budget, TFunctionRef<bool()> Cancel) {
		return CheckUnchangedProtectedCells(Size, Samples, Final, Mask, NoMask, NoMask, Budget, Cancel);
	};
	TestEqual(TEXT("exact work budget passes"), int32(Check(FIntVector(1, 1, 1), Samples, 64, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("insufficient budget fails"), int32(Check(FIntVector(1, 1, 1), Samples, 63, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("cancelled"), int32(Check(FIntVector(1, 1, 1), Samples, 64, AlwaysCancel).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TArray<int16> MissingHalo = Samples;
	MissingHalo.Pop();
	TestEqual(TEXT("missing halo rejected"), int32(Check(FIntVector(1, 1, 1), MissingHalo, 64, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("invalid dimensions rejected"), int32(Check(FIntVector(0, 1, 1), Samples, 64, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("dimension multiplication overflow rejected"),
		int32(Check(FIntVector(MAX_int32, MAX_int32, MAX_int32), Samples, MAX_int64, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::ArithmeticOverflow));
	Mask[0] = 2;
	TestEqual(TEXT("nonbinary mask rejected"), int32(Check(FIntVector(1, 1, 1), Samples, 64, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TArray<int16> LargeGrid;
	LargeGrid.Init(-1, 68 * 4 * 4);
	TArray<uint8> LargeProtection;
	LargeProtection.Init(1, 65);
	TArray<uint8> LargeEmpty;
	LargeEmpty.Init(0, 65);
	int32 Polls = 0;
	const auto CancelDuringScan = [&Polls]() {
		return ++Polls == 2;
	};
	const FVoxelLandM5Diagnostic Cancelled = CheckUnchangedProtectedCells(
		FIntVector(65, 1, 1), LargeGrid, LargeGrid, LargeProtection, LargeEmpty, LargeEmpty, 65 * 64, CancelDuringScan);
	TestEqual(
		TEXT("cancellation while scanning cells"), int32(Cancelled.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancellation reached the second batch"), Polls, 2);
	return true;
}
#endif
