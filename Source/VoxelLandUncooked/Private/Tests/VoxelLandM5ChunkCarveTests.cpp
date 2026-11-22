#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5ChunkCarve.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5ChunkCarveFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5ChunkCarvePoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Point;
	Point.X = X;
	Point.Y = Y;
	Point.Z = Z;
	return Point;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ChunkCarveSeamTest, "VoxelLand.M5.ChunkCarve.SharedSamples", M5ChunkCarveFlags)
bool FVoxelLandM5ChunkCarveSeamTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ChunkCarve;
	const FIntVector Size(3, 2, 2);
	TArray<int16> Baseline;
	Baseline.Init(-3000, 12);
	TArray<uint8> Carve;
	Carve.Init(1, 12);
	TArray<uint8> Protected;
	Protected.Init(0, 12);
	TArray<int64> NoPockets;
	auto Plan = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.RadiusCm = 150;
	Chamber.CenterCm = M5ChunkCarvePoint(0, -100, -100);
	Plan->Chambers.Add(Chamber);
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	Plans.Add(Plan);
	const auto NotCancelled = [] {
		return false;
	};
	TArray<int16> Left, Right, Lower, Upper, Below, Above;
	auto Run = [&](const FVoxelLandM5PointCm& Origin, TArray<int16>& Out) {
		return TryCarve(Baseline, Size, Origin, 100, 100, true, false, Plans, Carve, Carve, Protected, NoPockets, 1024,
			1000, NotCancelled, Out);
	};
	TestTrue(TEXT("negative X block"), Run(M5ChunkCarvePoint(-200, -100, -100), Left));
	TestTrue(TEXT("positive X block"), Run(M5ChunkCarvePoint(0, -100, -100), Right));
	if (Left.Num() != 12 || Right.Num() != 12)
	{
		AddError(TEXT("X block generation did not produce the expected samples"));
		return false;
	}
	TestEqual(TEXT("independent analytic center density"), Right[0], int16(1536));
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			TestEqual(TEXT("shared X plane"), Left[2 + 3 * Y + 6 * Z], Right[3 * Y + 6 * Z]);
		}
	}
	TestTrue(TEXT("negative Y block"), Run(M5ChunkCarvePoint(-100, -200, -100), Lower));
	TestTrue(TEXT("positive Y block"), Run(M5ChunkCarvePoint(-100, -100, -100), Upper));
	TestTrue(TEXT("negative Z block"), Run(M5ChunkCarvePoint(-100, -100, -200), Below));
	TestTrue(TEXT("positive Z block"), Run(M5ChunkCarvePoint(-100, -100, -100), Above));
	if (Lower.Num() != 12 || Upper.Num() != 12 || Below.Num() != 12 || Above.Num() != 12)
	{
		AddError(TEXT("Y or Z block generation did not produce the expected samples"));
		return false;
	}
	for (int32 I = 0; I < 6; ++I)
	{
		const int32 Y = I / 3;
		const int32 X = I % 3;
		TestEqual(TEXT("shared Z plane"), Below[6 + I], Above[I]);
		TestEqual(TEXT("shared Y plane"), Lower[X + 3 + 6 * Y], Upper[X + 6 * Y]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ChunkCarveFailureTest, "VoxelLand.M5.ChunkCarve.Failures", M5ChunkCarveFlags)
bool FVoxelLandM5ChunkCarveFailureTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ChunkCarve;
	const FIntVector Size(2, 1, 1);
	TArray<int16> Baseline = {-100, -200};
	TArray<uint8> Carve = {1, 1};
	TArray<uint8> Protected = {1, 0};
	TArray<int64> Pockets = {-25600, -25600};
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> EmptyPlans;
	TArray<int16> Out = {777};
	const auto NotCancelled = [] {
		return false;
	};
	const FVoxelLandM5PointCm Origin = M5ChunkCarvePoint(-100, 0, 0);
	auto Run = [&](const FIntVector& Dims, const FVoxelLandM5PointCm& Start, int64 Spacing, int64 Bytes, int64 Work,
				   TFunctionRef<bool()> Cancel) {
		return TryCarve(Baseline, Dims, Start, Spacing, 100, false, true, EmptyPlans, Carve, Carve, Protected, Pockets,
			Bytes, Work, Cancel, Out);
	};
	TestTrue(TEXT("pocket-only processing"), Run(Size, Origin, 100, 4, 4, NotCancelled));
	if (Out.Num() != 2)
	{
		AddError(TEXT("Pocket processing did not produce the expected samples"));
		return false;
	}
	TestEqual(TEXT("protection wins"), Out[0], int16(-100));
	TestEqual(TEXT("independent pocket conversion"), Out[1], int16(1024));
	Out = {777};
	TestFalse(TEXT("scratch budget rejected"), Run(Size, Origin, 100, 3, 4, NotCancelled));
	TestTrue(TEXT("budget failure atomic"), Out == TArray<int16>({777}));
	TestFalse(TEXT("work budget rejected"), Run(Size, Origin, 100, 4, 3, NotCancelled));
	TestFalse(TEXT("coordinates out of domain"), Run(Size, M5ChunkCarvePoint(1048576, 0, 0), 1, 4, 4, NotCancelled));
	TestFalse(TEXT("coordinate product overflow"), Run(Size, Origin, MAX_int64, 4, 4, NotCancelled));
	Carve[1] = 2;
	TestFalse(TEXT("invalid mask byte rejected"), Run(Size, Origin, 100, 4, 4, NotCancelled));
	Carve[1] = 1;
	Pockets.Pop();
	TestFalse(TEXT("missing pocket sample rejected"), Run(Size, Origin, 100, 4, 4, NotCancelled));
	Pockets.Add(-25600);
	int32 Checks = 0;
	const auto Cancel = [&Checks] {
		return ++Checks == 3;
	};
	TestFalse(TEXT("cancel after admission"), Run(Size, Origin, 100, 4, 4, Cancel));
	TestTrue(TEXT("all failures retain original array"), Out == TArray<int16>({777}));
	TArray<uint8> NoMask;
	TArray<int64> NoPocket;
	TestTrue(
		TEXT("disabled path copies without masks"), TryCarve(Baseline, Size, Origin, 0, 0, false, false, EmptyPlans,
														NoMask, NoMask, NoMask, NoPocket, 4, 2, NotCancelled, Out));
	TestTrue(TEXT("disabled path preserves baseline"), Out == Baseline);
	TArray<int16> LargeBaseline;
	LargeBaseline.Init(-1, 200);
	Out = {777};
	int32 CopyChecks = 0;
	const auto CancelMidCopy = [&CopyChecks] {
		return ++CopyChecks == 3;
	};
	TestFalse(TEXT("disabled copy cancels after first batch"),
		TryCarve(LargeBaseline, FIntVector(200, 1, 1), Origin, 0, 0, false, false, EmptyPlans, NoMask, NoMask, NoMask,
			NoPocket, 400, 200, CancelMidCopy, Out));
	TestTrue(TEXT("cancelled disabled copy preserves output"), Out == TArray<int16>({777}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ChunkCarveMaskTest, "VoxelLand.M5.ChunkCarve.IndependentMasks", M5ChunkCarveFlags)
bool FVoxelLandM5ChunkCarveMaskTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ChunkCarve;
	TArray<int16> Baseline = {-3000, -3000, -3000, -3000};
	// First point models an entrance: a planned route may cross the roof, but
	// pockets remain forbidden. Explicit protection at the last point wins both.
	TArray<uint8> Network = {1, 0, 1, 1};
	TArray<uint8> Pocket = {0, 1, 1, 1};
	TArray<uint8> Protected = {0, 0, 0, 1};
	TArray<int64> PocketValues = {-256000, -256000, -256000, -256000};
	auto Plan = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.RadiusCm = 500;
	Chamber.CenterCm = M5ChunkCarvePoint(0, 0, 0);
	Plan->Chambers.Add(Chamber);
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	Plans.Add(Plan);
	TArray<int16> Out;
	const auto NotCancelled = [] {
		return false;
	};
	TestTrue(TEXT("independent masks succeed"),
		TryCarve(Baseline, FIntVector(4, 1, 1), M5ChunkCarvePoint(0, 0, 0), 100, 100, true, true, Plans, Network,
			Pocket, Protected, PocketValues, 8, 16, NotCancelled, Out));
	if (Out.Num() != 4)
	{
		AddError(TEXT("Combined processing did not produce the expected samples"));
		return false;
	}
	TestEqual(TEXT("entrance permits network only"), Out[0], int16(5120));
	TestEqual(TEXT("pocket without network"), Out[1], int16(10240));
	TestEqual(TEXT("overlap uses union"), Out[2], int16(10240));
	TestEqual(TEXT("protected overrides both"), Out[3], int16(-3000));
	Out = {777};
	Pocket.Pop();
	TestFalse(TEXT("missing pocket mask rejected"),
		TryCarve(Baseline, FIntVector(4, 1, 1), M5ChunkCarvePoint(0, 0, 0), 100, 100, true, true, Plans, Network,
			Pocket, Protected, PocketValues, 8, 16, NotCancelled, Out));
	TestTrue(TEXT("mask failure is atomic"), Out == TArray<int16>({777}));
	return true;
}
#endif
