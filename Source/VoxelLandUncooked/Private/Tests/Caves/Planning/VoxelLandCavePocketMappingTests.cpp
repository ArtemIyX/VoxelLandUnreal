#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Generation/Caves/Planning/VoxelLandCaveComposition.h"
#include "Generation/Caves/Planning/VoxelLandCavePocketMapping.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PocketMappingTest, "VoxelLand.M5.Pockets.Mapping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PocketMappingTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::PocketMapping::TryMap;
	struct FCase
	{
		bool bEnabled;
		int32 Sample;
		int32 Threshold;
		int32 Amplitude;
		bool bHas;
		int64 Distance;
	};
	// These values are independently calculated integer ratios, not noise calls.
	const FCase Cases[] = {{true, -32768, 0, 400, false, 0}, {true, -32767, 0, 1, true, -1},
		{true, 0, 32768, 400, false, 0}, {true, 1, 32768, 400, true, -2}, {true, 32767, 65535, 400, false, 0},
		{true, 32767, 0, 1048576, true, -268435456}, {true, 0, 0, 400, true, -51201}, {false, 32767, 0, 400, false, 0},
		{true, 32767, 0, 0, false, 0}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
	{
		bool bHas = !Cases[Index].bHas;
		int64 Distance = 777;
		const FCase& Item = Cases[Index];
		const FString Label = FString::Printf(TEXT("case %d"), Index);
		TestTrue(*FString::Printf(TEXT("%s valid"), *Label),
			TryMap(Item.bEnabled, Item.Sample, Item.Threshold, Item.Amplitude, bHas, Distance));
		TestEqual(*FString::Printf(TEXT("%s presence"), *Label), bHas, Item.bHas);
		TestEqual(*FString::Printf(TEXT("%s Q8"), *Label), Distance, Item.Distance);
	}
	const FCase Invalid[] = {{true, -32769, 0, 1, false, 0}, {true, 32768, 0, 1, false, 0}, {true, 0, -1, 1, false, 0},
		{true, 0, 65536, 1, false, 0}, {true, 0, 0, -1, false, 0}, {true, 0, 0, 1048577, false, 0},
		{false, 0, 0, -1, false, 0}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Invalid); ++Index)
	{
		bool bHas = true;
		int64 Distance = 777;
		const FCase& Item = Invalid[Index];
		TestFalse(TEXT("invalid mapping rejected"),
			TryMap(Item.bEnabled, Item.Sample, Item.Threshold, Item.Amplitude, bHas, Distance));
		TestTrue(TEXT("invalid mapping preserves presence"), bHas);
		TestEqual(TEXT("invalid mapping preserves distance"), Distance, int64(777));
	}
	// A selected sample contributes air to solid under the existing hard
	// subtraction convention; an absent contribution keeps the input unchanged.
	FVoxelLandM5CavePlan Plan;
	FVoxelLandM5PointCm Position;
	int16 Result = 100;
	bool bHas = false;
	int64 Distance = 0;
	TestTrue(TEXT("map selected"), TryMap(true, 32767, 0, 400, bHas, Distance));
	TestTrue(TEXT("selected composition"),
		VoxelLand::M5::Composition::TryCompose(
			-100, Plan, Position, 100, true, true, false, bHas, Distance, [] { return false; }, Result));
	TestEqual(TEXT("selected sample carves"), Result, int16(4096));
	TestTrue(TEXT("map threshold equality"), TryMap(true, 0, 32768, 400, bHas, Distance));
	TestTrue(TEXT("absent composition"),
		VoxelLand::M5::Composition::TryCompose(
			-100, Plan, Position, 100, true, true, false, bHas, Distance, [] { return false; }, Result));
	TestEqual(TEXT("threshold equality leaves density"), Result, int16(-100));
	return true;
}
#endif
