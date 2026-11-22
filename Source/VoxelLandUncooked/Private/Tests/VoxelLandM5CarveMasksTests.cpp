#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CarveMasks.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5CarveMasksFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
constexpr int32 M5MaskX = 11;
constexpr int32 M5MaskY = 11;
constexpr int32 M5MaskZ = 15;
int32 M5MaskIndex(int32 X, int32 Y, int32 Z)
{
	return X + M5MaskX * (Y + M5MaskY * Z);
}
FVoxelLandM5PointCm M5MaskPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Point;
	Point.X = X;
	Point.Y = Y;
	Point.Z = Z;
	return Point;
}
void M5MaskFlatFixture(TArray<int16>& Samples, TArray<uint8>& Protection, TArray<uint8>& Entrance)
{
	Samples.SetNumUninitialized(M5MaskX * M5MaskY * M5MaskZ);
	Protection.Init(0, Samples.Num());
	Entrance.Init(0, Samples.Num());
	for (int32 Z = 0; Z < M5MaskZ; ++Z)
	{
		for (int32 Y = 0; Y < M5MaskY; ++Y)
		{
			for (int32 X = 0; X < M5MaskX; ++X)
			{
				Samples[M5MaskIndex(X, Y, Z)] = Z <= 8 ? -100 : 100;
			}
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CarveMasksFlatTest, "VoxelLand.M5.Masks.FlatAndEntrances", M5CarveMasksFlags)
bool FVoxelLandM5CarveMasksFlatTest::RunTest(const FString& Parameters)
{
	TArray<int16> Samples;
	TArray<uint8> Protection, Entrance;
	M5MaskFlatFixture(Samples, Protection, Entrance);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 1000000, Budget)))
	{
		return false;
	}
	const FIntVector Size(M5MaskX, M5MaskY, M5MaskZ);
	const FVoxelLandM5PointCm Origin = M5MaskPoint(-500, -500, -500);
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	FVoxelLandM5Diagnostic Error =
		FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 2), Samples, Protection,
			Entrance, 0, true, 200, 500, true, Budget.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("flat valid"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("output"), Masks.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("deep network allowed"), Masks->GetNetworkAllowed()[0], uint8(1));
	TestEqual(TEXT("deep bounded pocket allowed"), Masks->GetPocketAllowed()[0], uint8(1));
	TestEqual(TEXT("near surface network blocked"), Masks->GetNetworkAllowed()[1], uint8(0));
	TestEqual(TEXT("near surface pocket blocked"), Masks->GetPocketAllowed()[1], uint8(0));
	// The sample at z=6 touches the conservative surface halo. Entrance is a
	// pre-certified sample exception; it can never exempt explicit protection.
	Entrance[M5MaskIndex(5, 5, 6)] = 1;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 6), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("entrance valid"), Error.Code, EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("entrance network exception"), Masks->GetNetworkAllowed()[0], uint8(1));
	TestEqual(TEXT("no pocket entrance exception"), Masks->GetPocketAllowed()[0], uint8(0));
	Protection[M5MaskIndex(5, 5, 6)] = 1;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 6), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("protected valid"), Error.Code, EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("protection overrides entrance"), Masks->GetNetworkAllowed()[0], uint8(0));
	TestEqual(TEXT("protection passed to composition"), Masks->GetProtected()[0], uint8(1));
	// Two differently located windows request the same global sample at zero.
	Protection[M5MaskIndex(5, 5, 6)] = 0;
	Entrance[M5MaskIndex(5, 5, 6)] = 0;
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> A, B;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, true, 200, 500, true, Budget.ToSharedRef(), Never, A);
	if (!TestEqual(TEXT("first window"), Error.Code, EVoxelLandM5ErrorCode::None) || !A.IsValid())
	{
		return false;
	}
	const FVoxelLandM5PointCm ShiftedOrigin = M5MaskPoint(-600, -500, -500);
	Error = FVoxelLandM5CarveMasks::Build(Size, ShiftedOrigin, 100, FIntVector(6, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, true, 200, 500, true, Budget.ToSharedRef(), Never, B);
	if (!TestEqual(TEXT("shifted window"), Error.Code, EVoxelLandM5ErrorCode::None) || !B.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("same global network mask"), A->GetNetworkAllowed()[0], B->GetNetworkAllowed()[0]);
	TestEqual(TEXT("same global pocket mask"), A->GetPocketAllowed()[0], B->GetPocketAllowed()[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CarveMasksFailureTest, "VoxelLand.M5.Masks.OverhangAndFailure", M5CarveMasksFlags)
bool FVoxelLandM5CarveMasksFailureTest::RunTest(const FString& Parameters)
{
	TArray<int16> Samples;
	TArray<uint8> Protection, Entrance;
	M5MaskFlatFixture(Samples, Protection, Entrance);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 1000000, Budget)))
	{
		return false;
	}
	const FIntVector Size(M5MaskX, M5MaskY, M5MaskZ);
	const FVoxelLandM5PointCm Origin = M5MaskPoint(-500, -500, -500);
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	FVoxelLandM5Diagnostic Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5),
		FIntVector(1, 1, 1), Samples, Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("baseline fixture"), Error.Code, EVoxelLandM5ErrorCode::None) || !Masks.IsValid())
	{
		return false;
	}
	const auto Original = Masks;
	// Air enclosed under an overhang creates two extra vertical sign changes.
	Samples[M5MaskIndex(5, 5, 3)] = 100;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, true, 0, 1000, true, Budget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("ambiguous pocket rejected"), Error.Code, EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("prior output retained"), Masks == Original);
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("network mask handles enclosed surface"), Error.Code, EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("cavity roof protected"), Masks->GetNetworkAllowed()[0], uint8(0));
	const auto Current = Masks;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(2, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("missing halo rejected"), Error.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("missing halo preserves output"), Masks == Current);
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, false, Budget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("unknown water rejected"), Error.Code, EVoxelLandM5ErrorCode::Unproven);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (!TestTrue(TEXT("empty budget"), FVoxelLandM5Budget::TryCreate(0, 0, EmptyBudget)))
	{
		return false;
	}
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, EmptyBudget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("budget exhausted"), Error.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("budget failure preserves output"), Masks == Current);
	int32 Calls = 0;
	const auto CancelAfterStart = [&Calls] {
		return ++Calls == 3;
	};
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), CancelAfterStart, Masks);
	TestEqual(TEXT("cancel during search"), Error.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancel preserves output"), Masks == Current);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CarveMasksRoofBoundaryTest, "VoxelLand.M5.Masks.PositiveRoofBoundary", M5CarveMasksFlags)
bool FVoxelLandM5CarveMasksRoofBoundaryTest::RunTest(const FString& Parameters)
{
	// A plane lies between samples z=10 and z=11. At spacing 100 cm and
	// roof 100 cm, the conservative radius is three cells. The search's last
	// mixed-cell origin is z=10: it is included at output z=7, but not z=6.
	const FIntVector Size(15, 15, 20);
	const int32 Total = Size.X * Size.Y * Size.Z;
	TArray<int16> Samples;
	TArray<uint8> Protection, Entrance;
	Samples.SetNumUninitialized(Total);
	Protection.Init(0, Total);
	Entrance.Init(0, Total);
	for (int32 Z = 0; Z < Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				Samples[X + Size.X * (Y + Size.Y * Z)] = Z <= 10 ? -1 : 1;
			}
		}
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 1000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	const auto Error = FVoxelLandM5CarveMasks::Build(
		Size, M5MaskPoint(-700, -700, -700), 100, FIntVector(7, 7, 6), FIntVector(1, 1, 2), Samples, Protection,
		Entrance, 100, false, 0, 0, true, Budget.ToSharedRef(), [] { return false; }, Masks);
	if (!TestEqual(TEXT("positive roof valid"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("mask exists"), Masks.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("outside conservative dilation"), Masks->GetNetworkAllowed()[0], uint8(1));
	TestEqual(TEXT("last included mixed cell blocks sample"), Masks->GetNetworkAllowed()[1], uint8(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CarveMasksWindowTest, "VoxelLand.M5.Masks.ShiftedNonuniformWindows", M5CarveMasksFlags)
bool FVoxelLandM5CarveMasksWindowTest::RunTest(const FString& Parameters)
{
	const FIntVector Size(15, 15, 20);
	const int32 Total = Size.X * Size.Y * Size.Z;
	TArray<int16> A, B;
	TArray<uint8> Protection, Entrance;
	A.SetNumUninitialized(Total);
	B.SetNumUninitialized(Total);
	Protection.Init(0, Total);
	Entrance.Init(0, Total);
	// Independently construct each window from global coordinates. The surface
	// steps up at world X=0, so equal values are not an artifact of a flat field.
	for (int32 Z = 0; Z < Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const int32 I = X + Size.X * (Y + Size.Y * Z);
				const int32 WorldZ = -700 + Z * 100;
				const int32 WorldXA = -700 + X * 100;
				const int32 WorldXB = -800 + X * 100;
				A[I] = WorldZ <= (WorldXA < 0 ? 300 : 400) ? -1 : 1;
				B[I] = WorldZ <= (WorldXB < 0 ? 300 : 400) ? -1 : 1;
			}
		}
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 1000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> First, Second;
	const auto Never = [] {
		return false;
	};
	auto Error = FVoxelLandM5CarveMasks::Build(Size, M5MaskPoint(-700, -700, -700), 100, FIntVector(6, 7, 6),
		FIntVector(2, 1, 1), A, Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, First);
	if (!TestEqual(TEXT("first window valid"), Error.Code, EVoxelLandM5ErrorCode::None) || !First.IsValid())
	{
		return false;
	}
	Error = FVoxelLandM5CarveMasks::Build(Size, M5MaskPoint(-800, -700, -700), 100, FIntVector(7, 7, 6),
		FIntVector(2, 1, 1), B, Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Second);
	if (!TestEqual(TEXT("second window valid"), Error.Code, EVoxelLandM5ErrorCode::None) || !Second.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("negative world sample matches"), First->GetNetworkAllowed()[0], Second->GetNetworkAllowed()[0]);
	TestEqual(TEXT("zero world sample matches"), First->GetNetworkAllowed()[1], Second->GetNetworkAllowed()[1]);
	// Corrupt a shared global sample in B. The helper derives a different mask;
	// an independent density-overlap check must reject these mismatched inputs.
	B[8 + Size.X * (7 + Size.Y * 6)] = 1;
	Error = FVoxelLandM5CarveMasks::Build(Size, M5MaskPoint(-800, -700, -700), 100, FIntVector(7, 7, 6),
		FIntVector(2, 1, 1), B, Protection, Entrance, 0, false, 0, 0, true, Budget.ToSharedRef(), Never, Second);
	if (!TestEqual(TEXT("corrupt window computed"), Error.Code, EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("baseline zero sample remains permitted"), First->GetNetworkAllowed()[1], uint8(1));
	TestEqual(TEXT("corrupt sample changes local mask"), Second->GetNetworkAllowed()[1], uint8(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CarveMasksAdmissionTest, "VoxelLand.M5.Masks.AdmissionAndBinaryInputs", M5CarveMasksFlags)
bool FVoxelLandM5CarveMasksAdmissionTest::RunTest(const FString& Parameters)
{
	TArray<int16> Samples;
	TArray<uint8> Protection, Entrance;
	M5MaskFlatFixture(Samples, Protection, Entrance);
	const FIntVector Size(M5MaskX, M5MaskY, M5MaskZ);
	const FVoxelLandM5PointCm Origin = M5MaskPoint(-500, -500, -500);
	const auto Never = [] {
		return false;
	};
	// A single sample at roof=0 visits 6^3 cells, charging 12 per cell,
	// eight units for output and one final unit: 2601 work, 4096+3 bytes.
	constexpr int64 ExactWork = 2601;
	constexpr int64 ExactBytes = 4099;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Exact, LessWork, LessMemory;
	if (!TestTrue(TEXT("exact budget"), FVoxelLandM5Budget::TryCreate(ExactBytes, ExactWork, Exact))
		|| !TestTrue(TEXT("work budget"), FVoxelLandM5Budget::TryCreate(ExactBytes, ExactWork - 1, LessWork))
		|| !TestTrue(TEXT("memory budget"), FVoxelLandM5Budget::TryCreate(ExactBytes - 1, ExactWork, LessMemory)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	auto Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, Exact.ToSharedRef(), Never, Masks);
	if (!TestEqual(TEXT("exact admission"), Error.Code, EVoxelLandM5ErrorCode::None) || !Masks.IsValid())
	{
		return false;
	}
	const auto Original = Masks;
	int64 Live = 0, Peak = 0, Work = 0;
	Exact->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("exact work charged"), Work, ExactWork);
	TestEqual(TEXT("output memory retained"), Live, ExactBytes);
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, LessWork.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("one less work fails"), Error.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("work failure retains output"), Masks == Original);
	LessWork->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failed work releases memory"), Live, int64(0));
	TestEqual(TEXT("failed work not charged"), Work, int64(0));
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, LessMemory.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("one less byte fails"), Error.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("memory failure retains output"), Masks == Original);
	LessMemory->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("memory failure does not charge work"), Work, int64(0));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BinaryBudget;
	if (!TestTrue(TEXT("binary budget"), FVoxelLandM5Budget::TryCreate(100000, 100000, BinaryBudget)))
	{
		return false;
	}
	Protection[M5MaskIndex(5, 5, 5)] = 2;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, BinaryBudget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("nonbinary protection rejected"), Error.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid protection retains output"), Masks == Original);
	Protection[M5MaskIndex(5, 5, 5)] = 0;
	Entrance[M5MaskIndex(5, 5, 5)] = 255;
	Error = FVoxelLandM5CarveMasks::Build(Size, Origin, 100, FIntVector(5, 5, 5), FIntVector(1, 1, 1), Samples,
		Protection, Entrance, 0, false, 0, 0, true, BinaryBudget.ToSharedRef(), Never, Masks);
	TestEqual(TEXT("nonbinary entrance rejected"), Error.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid entrance retains output"), Masks == Original);
	return true;
}
#endif
