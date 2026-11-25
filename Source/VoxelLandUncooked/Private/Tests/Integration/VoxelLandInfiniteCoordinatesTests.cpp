#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Coordinates/VoxelLandInfiniteCoordinates.h"
#include "Generation/Caves/Planning/VoxelLandCaveSeed.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandInfiniteSplitJoinTest, "VoxelLand.Infinite.Coordinates.SplitJoin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandInfiniteSplitJoinTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::InfiniteCoordinates;
	const struct FCase
	{
		int64 Chunk;
		int64 Size;
		int64 Region;
		int64 Local;
	} Cases[] = {{-33, 32, -2, 31}, {-32, 32, -1, 0}, {-1, 32, -1, 31}, {0, 32, 0, 0}, {31, 32, 0, 31}, {32, 32, 1, 0},
		{MIN_int64, 3, -3074457345618258603ll, 1}, {MAX_int64, 3, 3074457345618258602ll, 1},
		{MIN_int64, 1, MIN_int64, 0}, {MAX_int64, MAX_int64, 1, 0}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
	{
		int64 Region = 17;
		int64 Local = 19;
		const bool bSplit = TrySplitChunk(Cases[Index].Chunk, Cases[Index].Size, Region, Local);
		TestTrue(*FString::Printf(TEXT("case %d splits"), Index), bSplit);
		if (!bSplit)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("case %d region"), Index), Region, Cases[Index].Region);
		TestEqual(*FString::Printf(TEXT("case %d local"), Index), Local, Cases[Index].Local);
		int64 Joined = 23;
		TestTrue(
			*FString::Printf(TEXT("case %d joins"), Index), TryJoinChunk(Region, Local, Cases[Index].Size, Joined));
		TestEqual(*FString::Printf(TEXT("case %d roundtrip"), Index), Joined, Cases[Index].Chunk);
	}
	int64 A = 17;
	int64 B = 19;
	TestFalse(TEXT("zero size"), TrySplitChunk(1, 0, A, B));
	TestEqual(TEXT("split failure region unchanged"), A, int64(17));
	TestEqual(TEXT("split failure local unchanged"), B, int64(19));
	TestFalse(TEXT("aliased outputs"), TrySplitChunk(1, 32, A, A));
	TestEqual(TEXT("alias failure unchanged"), A, int64(17));
	TestFalse(TEXT("offset out of range"), TryJoinChunk(-1, 32, 32, A));
	TestFalse(TEXT("negative offset"), TryJoinChunk(0, -1, 32, A));
	TestFalse(TEXT("positive overflow"), TryJoinChunk(MAX_int64, 0, 2, A));
	TestFalse(TEXT("negative overflow"), TryJoinChunk(MIN_int64, 0, 2, A));
	TestEqual(TEXT("join failures unchanged"), A, int64(17));

	// Independent exhaustive integer reference within a domain where products
	// cannot overflow. Check the defining inequalities, not the implementation.
	for (int64 Chunk = -200; Chunk <= 200; ++Chunk)
	{
		for (int64 Size = 1; Size <= 37; ++Size)
		{
			int64 Region = 0;
			int64 Local = 0;
			if (!TrySplitChunk(Chunk, Size, Region, Local) || Region * Size + Local != Chunk || Local < 0
				|| Local >= Size || Region * Size > Chunk || (Region + 1) * Size <= Chunk)
			{
				AddError(FString::Printf(TEXT("Incorrect split for %lld with size %lld"), Chunk, Size));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandInfiniteSamplesTest, "VoxelLand.Infinite.Coordinates.Samples",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandInfiniteSamplesTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::InfiniteCoordinates;
	constexpr int64 Origin = 123456789012345ll;
	int64 Left = 0;
	int64 Right = 0;
	TestTrue(TEXT("left side of shared plane"), TrySampleRelativeCm(Origin - 1, Origin, 32, 32, 100, 10000, Left));
	TestTrue(TEXT("right side of shared plane"), TrySampleRelativeCm(Origin, Origin, 0, 32, 100, 10000, Right));
	TestEqual(TEXT("same global sample"), Left, Right);
	TestEqual(TEXT("shared plane lies at origin"), Left, int64(0));
	TestTrue(TEXT("left positive halo"), TrySampleRelativeCm(Origin - 1, Origin, 33, 32, 100, 10000, Left));
	TestTrue(TEXT("right sample one"), TrySampleRelativeCm(Origin, Origin, 1, 32, 100, 10000, Right));
	TestEqual(TEXT("halo agrees"), Left, Right);
	TestEqual(TEXT("one voxel"), Left, int64(100));
	TestTrue(TEXT("negative voxel"), TrySampleRelativeCm(Origin, Origin, -1, 32, 100, 10000, Left));
	TestEqual(TEXT("negative coordinate"), Left, int64(-100));
	TestTrue(TEXT("far logical index remains local"), TrySampleRelativeCm(MAX_int64, MAX_int64, 0, 32, 100, 0, Left));
	TestEqual(TEXT("far local zero"), Left, int64(0));
	Left = 987;
	TestFalse(TEXT("out of local bound"), TrySampleRelativeCm(Origin, Origin, 1, 32, 100, 99, Left));
	TestFalse(
		TEXT("coordinate subtraction overflow"), TrySampleRelativeCm(MIN_int64, MAX_int64, 0, 32, 100, 10000, Left));
	TestFalse(TEXT("scale overflow"), TrySampleRelativeCm(1, 0, 0, MAX_int64 - 1, 2, MAX_int64, Left));
	TestFalse(TEXT("unsupported halo"), TrySampleRelativeCm(0, 0, -2, 32, 100, 10000, Left));
	TestFalse(TEXT("negative envelope"), TrySampleRelativeCm(0, 0, 0, 32, 100, -1, Left));
	TestEqual(TEXT("failed sample unchanged"), Left, int64(987));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandInfiniteFaceTest, "VoxelLand.Infinite.Coordinates.SharedFace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandInfiniteFaceTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::InfiniteCoordinates;
	int64 AX = 0, AY = 0, BX = 0, BY = 0;
	uint8 Axis = 9;
	TestTrue(TEXT("negative shared face"), TryCanonicalFace(-1, 4, -2, 4, AX, AY, BX, BY, Axis));
	TestEqual(TEXT("first x"), AX, int64(-2));
	TestEqual(TEXT("first y"), AY, int64(4));
	TestEqual(TEXT("second x"), BX, int64(-1));
	TestEqual(TEXT("second y"), BY, int64(4));
	TestEqual(TEXT("x face"), Axis, uint8(0));
	TArray<uint8> Forward;
	TArray<uint8> Reverse;
	const FGuid Stage(1, 2, 3, 4);
	TestTrue(TEXT("portal packet forward"), VoxelLand::M5::Seed::TryPortalPacket(7, Stage, 9, AX, AY, BX, BY, Forward));
	TestTrue(TEXT("portal packet reverse"), VoxelLand::M5::Seed::TryPortalPacket(7, Stage, 9, BX, BY, AX, AY, Reverse));
	TestTrue(TEXT("portal namespace is order-independent"), Forward == Reverse);
	TestTrue(TEXT("positive edge face"),
		TryCanonicalFace(MAX_int64, MIN_int64, MAX_int64 - 1, MIN_int64, AX, AY, BX, BY, Axis));
	TestEqual(TEXT("positive edge first"), AX, MAX_int64 - 1);
	TestTrue(TEXT("negative y face"), TryCanonicalFace(2, MIN_int64 + 1, 2, MIN_int64, AX, AY, BX, BY, Axis));
	TestEqual(TEXT("y face"), Axis, uint8(1));
	AX = 10;
	AY = 11;
	BX = 12;
	BY = 13;
	Axis = 14;
	TestFalse(TEXT("diagonal is not face adjacent"), TryCanonicalFace(0, 0, 1, 1, AX, AY, BX, BY, Axis));
	TestEqual(TEXT("invalid first x unchanged"), AX, int64(10));
	TestEqual(TEXT("invalid first y unchanged"), AY, int64(11));
	TestEqual(TEXT("invalid second x unchanged"), BX, int64(12));
	TestEqual(TEXT("invalid second y unchanged"), BY, int64(13));
	TestEqual(TEXT("invalid axis unchanged"), Axis, uint8(14));
	TestFalse(TEXT("same region"), TryCanonicalFace(0, 0, 0, 0, AX, AY, BX, BY, Axis));
	TestFalse(TEXT("aliased face outputs"), TryCanonicalFace(0, 0, 1, 0, AX, AX, BX, BY, Axis));
	TestEqual(TEXT("alias failure unchanged"), AX, int64(10));
	return true;
}
#endif
