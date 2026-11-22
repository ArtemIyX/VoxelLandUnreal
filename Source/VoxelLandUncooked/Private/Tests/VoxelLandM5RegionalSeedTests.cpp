#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5RegionalSeed.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5RegionalSeedFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FString M5RegionalSeedHex(const TArray<uint8>& Bytes)
{
	return BytesToHex(Bytes.GetData(), Bytes.Num()).ToLower();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5RegionalSeedVectorsTest, "VoxelLand.M5.Seed.RegionalVectors", M5RegionalSeedFlags)
bool FVoxelLandM5RegionalSeedVectorsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::RegionalSeed;
	const FGuid Guid(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	constexpr uint64 Salt = 0x0102030405060708ULL;
	TArray<uint8> Packet;
	TArray<uint8> Digest;
	TestTrue(TEXT("regional stream packet"), TryStreamPacket(1, -1, Guid, Salt, -2, 4, 42, 3, 5, Packet));
	TestEqual(TEXT("regional stream bytes"), M5RegionalSeedHex(Packet),
		FString(TEXT(
			"564c4d355245470001000000feffffffffffffff0400000000000000564c4d35524e47000100000001ffffffffffffffff67452301efcdab894030201098badcfe08070605040302012a000000000000000300000005000000")));
	TestTrue(TEXT("regional stream digest"), TryStreamDigest(1, -1, Guid, Salt, -2, 4, 42, 3, 5, Digest));
	TestEqual(TEXT("regional stream SHA-256"), M5RegionalSeedHex(Digest),
		FString(TEXT("31d9c55e03bfba29ea3b8e61237cb2a900addfed35fcb5e5f74ccac531da6eef")));
	uint64 Word = 0;
	TestTrue(TEXT("regional stream word"), VoxelLand::M5::Seed::TryDigestWord(Digest, Word));
	TestEqual(TEXT("regional little-endian word"), Word, uint64(0x29babf035ec5d931ULL));

	TestTrue(TEXT("feature packet"), TryFeaturePacket(-1, Guid, Salt, -2, 4, 1, 42, 0, Packet));
	TestEqual(TEXT("feature packet bytes"), M5RegionalSeedHex(Packet),
		FString(TEXT(
			"564c4d354649440001000000ffffffffffffffff67452301efcdab894030201098badcfe0807060504030201feffffffffffffff0400000000000000012a000000000000000000000000000000")));
	TestTrue(TEXT("feature digest"), TryFeatureDigest(-1, Guid, Salt, -2, 4, 1, 42, 0, Digest));
	TestEqual(TEXT("feature SHA-256"), M5RegionalSeedHex(Digest),
		FString(TEXT("619a35195bcbe32a37a67f5d58103c6bba89b9eccd01f627537712475c5e11fb")));
	TArray<uint8> Other;
	TestTrue(TEXT("neighbor stream"), TryStreamDigest(1, -1, Guid, Salt, -1, 4, 42, 3, 5, Other));
	TArray<uint8> Original;
	TestTrue(TEXT("original stream"), TryStreamDigest(1, -1, Guid, Salt, -2, 4, 42, 3, 5, Original));
	TestTrue(TEXT("different regions use different streams"), Original != Other);
	TestTrue(TEXT("neighbor feature"), TryFeatureDigest(-1, Guid, Salt, -1, 4, 1, 42, 0, Other));
	TestTrue(TEXT("different regions use different IDs"), Digest != Other);
	TestTrue(TEXT("entrance feature"), TryFeatureDigest(-1, Guid, Salt, -2, 4, 3, 42, 0, Other));
	TestTrue(TEXT("feature kinds are separated"), Digest != Other);
	TestTrue(TEXT("canonical edge"), TryFeatureDigest(-1, Guid, Salt, -2, 4, 2, 2, 9, Digest));
	TestEqual(TEXT("edge digest"), M5RegionalSeedHex(Digest),
		FString(TEXT("19e17dde94cf78128b833efb91c09d982bcb7476018dcece5cf6d22476f16580")));
	TestTrue(TEXT("reversed edge"), TryFeatureDigest(-1, Guid, Salt, -2, 4, 2, 9, 2, Other));
	TestTrue(TEXT("edge order does not matter"), Digest == Other);
	TestTrue(TEXT("signed coordinate limits"), TryStreamPacket(1, MIN_int64, Guid, MAX_uint64, MIN_int64, MAX_int64,
												   MAX_uint64, MAX_uint32, MAX_uint32, Packet));
	TestEqual(TEXT("fixed regional packet length"), Packet.Num(), 89);

	Packet = {7, 8};
	Digest = {7, 8};
	TestFalse(TEXT("unknown stream namespace"), TryStreamPacket(0, 0, Guid, 0, 0, 0, 0, 0, 0, Packet));
	TestFalse(TEXT("unknown stream digest"), TryStreamDigest(6, 0, Guid, 0, 0, 0, 0, 0, 0, Digest));
	TestFalse(TEXT("unknown kind"), TryFeaturePacket(0, Guid, 0, 0, 0, 0, 0, 0, Packet));
	TestFalse(TEXT("candidate extra key"), TryFeaturePacket(0, Guid, 0, 0, 0, 1, 0, 1, Packet));
	TestFalse(TEXT("edge identical endpoints"), TryFeatureDigest(0, Guid, 0, 0, 0, 2, 5, 5, Digest));
	TestTrue(TEXT("invalid packet unchanged"), Packet == TArray<uint8>({7, 8}));
	TestTrue(TEXT("invalid digest unchanged"), Digest == TArray<uint8>({7, 8}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BoundedShaTest, "VoxelLand.M5.Seed.BoundedSha256", M5RegionalSeedFlags)
bool FVoxelLandM5BoundedShaTest::RunTest(const FString& Parameters)
{
	// Independent hashlib vectors for ascending byte sequences at both padding boundaries.
	const int32 Lengths[] = {0, 55, 56, 63, 64, 119};
	const TCHAR* Expected[] = {TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
		TEXT("463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"),
		TEXT("da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"),
		TEXT("29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"),
		TEXT("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"),
		TEXT("da18797ed7c3a777f0847f429724a2d8cd5138e6ed2895c3fa1a6d39d18f7ec6")};
	for (int32 Case = 0; Case < UE_ARRAY_COUNT(Lengths); ++Case)
	{
		TArray<uint8> Bytes;
		for (int32 Index = 0; Index < Lengths[Case]; ++Index)
		{
			Bytes.Add(static_cast<uint8>(Index));
		}
		TArray<uint8> Digest;
		TestTrue(*FString::Printf(TEXT("SHA length %d"), Lengths[Case]), VoxelLand::M5::Seed::TrySha256(Bytes, Digest));
		TestEqual(
			*FString::Printf(TEXT("SHA digest %d"), Lengths[Case]), M5RegionalSeedHex(Digest), FString(Expected[Case]));
	}
	TArray<uint8> TooLong;
	TooLong.SetNumZeroed(120);
	TArray<uint8> Digest = {7, 8};
	TestFalse(TEXT("length 120 rejected"), VoxelLand::M5::Seed::TrySha256(TooLong, Digest));
	TestTrue(TEXT("failure preserves output"), Digest == TArray<uint8>({7, 8}));
	return true;
}
#endif
