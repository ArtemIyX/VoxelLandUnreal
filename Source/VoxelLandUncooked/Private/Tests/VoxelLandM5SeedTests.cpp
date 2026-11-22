#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Seed.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SeedFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FString M5SeedHex(const TArray<uint8>& Bytes)
{
	return BytesToHex(Bytes.GetData(), Bytes.Num()).ToLower();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SeedVectorsTest, "VoxelLand.M5.Seed.ReferenceVectors", M5SeedFlags)
bool FVoxelLandM5SeedVectorsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Seed;
	TArray<uint8> Packet;
	TArray<uint8> Digest;
	uint64 Word = 0;
	TestTrue(TEXT("zero stream packet"), TryStreamPacket(1, 0, FGuid(), 0, 0, 0, 0, Packet));
	TestEqual(TEXT("zero packet bytes"), M5SeedHex(Packet),
		FString(TEXT(
			"564c4d35524e47000100000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000")));
	TestTrue(TEXT("zero stream digest"), TryStreamDigest(1, 0, FGuid(), 0, 0, 0, 0, Digest));
	TestEqual(TEXT("zero SHA-256"), M5SeedHex(Digest),
		FString(TEXT("cb337d2ac7ed9a6450b2de10f2391268b60de4bb9c84627797ff7621841dc678")));
	TestTrue(TEXT("digest word"), TryDigestWord(Digest, Word));
	TestEqual(TEXT("digest LE word"), Word, uint64(0x649aedc72a7d33cbULL));

	const FGuid Guid(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	constexpr uint64 Salt = 0x0102030405060708ULL;
	TestTrue(TEXT("negative seed packet"), TryStreamPacket(1, -1, Guid, Salt, 42, 3, 5, Packet));
	TestEqual(TEXT("negative seed bytes"), M5SeedHex(Packet),
		FString(TEXT(
			"564c4d35524e47000100000001ffffffffffffffff67452301efcdab894030201098badcfe08070605040302012a000000000000000300000005000000")));
	TestTrue(TEXT("negative seed digest"), TryStreamDigest(1, -1, Guid, Salt, 42, 3, 5, Digest));
	TestEqual(TEXT("negative seed SHA-256"), M5SeedHex(Digest),
		FString(TEXT("d3722abe283166cd59ae046d71ebcf50a18179f59476a036027fd6c1d4bfe1ff")));
	TestTrue(TEXT("edge namespace"), TryStreamDigest(2, -1, Guid, Salt, 42, 3, 5, Digest));
	TestEqual(TEXT("edge SHA-256"), M5SeedHex(Digest),
		FString(TEXT("a4e8fc4fd044ba36cce8759d9222b9374257d64d1e4a1d328625261518e8cd9b")));

	TestTrue(TEXT("portal packet"), TryPortalPacket(-1, Guid, Salt, -2, 4, -1, 4, Packet));
	TestEqual(TEXT("portal bytes"), M5SeedHex(Packet),
		FString(TEXT(
			"564c4d355052540001000000ffffffffffffffff67452301efcdab894030201098badcfe0807060504030201feffffffffffffff0400000000000000ffffffffffffffff040000000000000000")));
	TestTrue(TEXT("portal digest"), TryPortalDigest(-1, Guid, Salt, -2, 4, -1, 4, Digest));
	TestEqual(TEXT("portal SHA-256"), M5SeedHex(Digest),
		FString(TEXT("8dad505e95b98163002956f877aa946a7a62c728c8fc6c8000fa95fbb6d34323")));
	TestTrue(TEXT("portal digest word"), TryDigestWord(Digest, Word));
	TestEqual(TEXT("portal ID"), Word, uint64(0x6381b9955e50ad8dULL));
	TArray<uint8> Reversed;
	TestTrue(TEXT("reversed face"), TryPortalDigest(-1, Guid, Salt, -1, 4, -2, 4, Reversed));
	TestTrue(TEXT("face is independent of request side"), Digest == Reversed);
	TestTrue(TEXT("Y face canonical"), TryPortalDigest(1, Guid, Salt, 3, -2, 3, -1, Digest));
	TestTrue(TEXT("Y face reverse"), TryPortalDigest(1, Guid, Salt, 3, -1, 3, -2, Reversed));
	TestTrue(TEXT("Y face order"), Digest == Reversed);

	Packet = {7, 8};
	TestFalse(TEXT("invalid namespace"), TryStreamPacket(0, 0, Guid, 0, 0, 0, 0, Packet));
	TestTrue(TEXT("invalid namespace leaves packet"), Packet == TArray<uint8>({7, 8}));
	TestFalse(TEXT("non-adjacent"), TryPortalPacket(0, Guid, 0, MIN_int64, 0, MAX_int64, 0, Packet));
	TestFalse(TEXT("diagonal"), TryPortalPacket(0, Guid, 0, 0, 0, 1, 1, Packet));
	TestFalse(TEXT("identical"), TryPortalPacket(0, Guid, 0, 0, 0, 0, 0, Packet));
	TestTrue(TEXT("bad faces leave packet"), Packet == TArray<uint8>({7, 8}));
	Word = 123;
	TestFalse(TEXT("short digest"), TryDigestWord(Packet, Word));
	TestEqual(TEXT("bad digest leaves word"), Word, uint64(123));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SeedUniformTest, "VoxelLand.M5.Seed.UniformRange", M5SeedFlags)
bool FVoxelLandM5SeedUniformTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Seed;
	uint64 Value = 99;
	uint32 Used = 99;
	uint32 Calls = 0;
	const auto NeverCancel = []() {
		return false;
	};
	const auto RejectThenAccept = [&Calls](uint32 DrawIndex, uint64& Out) {
		++Calls;
		Out = DrawIndex == 17 ? 5 : 16;
		return true;
	};
	TestTrue(
		TEXT("rejection then unbiased acceptance"), TryUniform(10, 17, 2, RejectThenAccept, NeverCancel, Value, Used));
	TestEqual(TEXT("16 modulo 10"), Value, uint64(6));
	TestEqual(TEXT("draw count"), Used, uint32(2));
	TestEqual(TEXT("source calls"), Calls, uint32(2));
	Value = 99;
	Used = 99;
	Calls = 0;
	TestFalse(TEXT("exhausted rejection budget"), TryUniform(10, 17, 1, RejectThenAccept, NeverCancel, Value, Used));
	TestEqual(TEXT("exhaustion leaves value"), Value, uint64(99));
	TestEqual(TEXT("exhaustion leaves used"), Used, uint32(99));
	const auto AlwaysReject = [&Calls](uint32, uint64& Out) {
		++Calls;
		Out = 0;
		return true;
	};
	Calls = 0;
	TestFalse(TEXT("draw index overflow"), TryUniform(10, MAX_uint32, 2, AlwaysReject, NeverCancel, Value, Used));
	TestEqual(TEXT("no wrapped draw"), Calls, uint32(1));
	TestFalse(TEXT("zero range"), TryUniform(0, 0, 1, AlwaysReject, NeverCancel, Value, Used));
	TestFalse(TEXT("zero budget"), TryUniform(10, 0, 0, AlwaysReject, NeverCancel, Value, Used));
	const auto Cancel = []() {
		return true;
	};
	Calls = 0;
	TestFalse(TEXT("cancellation"), TryUniform(10, 0, 2, AlwaysReject, Cancel, Value, Used));
	TestEqual(TEXT("cancel before source"), Calls, uint32(0));
	TestEqual(TEXT("all failures leave value"), Value, uint64(99));
	TestEqual(TEXT("all failures leave used"), Used, uint32(99));
	const auto Maximum = [](uint32, uint64& Out) {
		Out = MAX_uint64;
		return true;
	};
	TestTrue(TEXT("range one"), TryUniform(1, 0, 1, Maximum, NeverCancel, Value, Used));
	TestEqual(TEXT("range one value"), Value, uint64(0));
	TestTrue(TEXT("maximum representable range"), TryUniform(MAX_uint64, 0, 1, Maximum, NeverCancel, Value, Used));
	TestEqual(TEXT("maximum modulo range"), Value, uint64(0));

	int64 Signed = 77;
	Used = 99;
	const auto Zero = [](uint32, uint64& Out) {
		Out = 0;
		return true;
	};
	TestTrue(TEXT("full signed range minimum"),
		TryUniformSigned(MIN_int64, MAX_int64, 0, 1, Zero, NeverCancel, Signed, Used));
	TestEqual(TEXT("full signed minimum"), Signed, int64(MIN_int64));
	TestTrue(TEXT("full signed range maximum"),
		TryUniformSigned(MIN_int64, MAX_int64, 0, 1, Maximum, NeverCancel, Signed, Used));
	TestEqual(TEXT("full signed maximum"), Signed, int64(MAX_int64));
	TestTrue(TEXT("singleton signed range"), TryUniformSigned(-7, -7, 0, 1, Maximum, NeverCancel, Signed, Used));
	TestEqual(TEXT("singleton result"), Signed, int64(-7));
	const auto DrawThree = [](uint32, uint64& Out) {
		Out = 3;
		return true;
	};
	TestTrue(TEXT("cross-zero interval"), TryUniformSigned(-2, 2, 0, 1, DrawThree, NeverCancel, Signed, Used));
	TestEqual(TEXT("cross-zero expected"), Signed, int64(1));
	const auto DrawFour = [](uint32, uint64& Out) {
		Out = 4;
		return true;
	};
	TestTrue(TEXT("near signed minimum"),
		TryUniformSigned(MIN_int64, MIN_int64 + 4, 0, 1, DrawFour, NeverCancel, Signed, Used));
	TestEqual(TEXT("near minimum expected"), Signed, int64(MIN_int64 + 4));
	const auto DrawTwo = [](uint32, uint64& Out) {
		Out = 2;
		return true;
	};
	TestTrue(TEXT("near signed maximum"),
		TryUniformSigned(MAX_int64 - 4, MAX_int64, 0, 1, DrawTwo, NeverCancel, Signed, Used));
	TestEqual(TEXT("near maximum expected"), Signed, int64(MAX_int64 - 2));
	TestTrue(TEXT("all but maximum signed value"),
		TryUniformSigned(MIN_int64, MAX_int64 - 1, 0, 1, Maximum, NeverCancel, Signed, Used));
	TestEqual(TEXT("wide interval wrap-free result"), Signed, int64(MIN_int64));
	Signed = 77;
	Used = 99;
	TestFalse(TEXT("reversed signed range"), TryUniformSigned(1, -1, 0, 1, Zero, NeverCancel, Signed, Used));
	TestEqual(TEXT("reversed leaves signed output"), Signed, int64(77));
	TestEqual(TEXT("reversed leaves count"), Used, uint32(99));
	int32 Polls = 0;
	const auto CancelAfterSource = [&Polls]() {
		return ++Polls == 2;
	};
	TestFalse(TEXT("cancel before signed publication"),
		TryUniformSigned(MIN_int64, MAX_int64, 0, 1, Zero, CancelAfterSource, Signed, Used));
	TestEqual(TEXT("cancel leaves signed output"), Signed, int64(77));
	TestEqual(TEXT("cancel leaves count"), Used, uint32(99));

	const auto FailingSource = [](uint32, uint64&) {
		return false;
	};
	TestFalse(TEXT("signed callback failure"), TryUniformSigned(-2, 2, 0, 3, FailingSource, NeverCancel, Signed, Used));
	TestEqual(TEXT("callback failure leaves signed output"), Signed, int64(77));
	TestEqual(TEXT("callback failure leaves count"), Used, uint32(99));
	bool bCancelAfterRejected = false;
	uint32 RejectedCalls = 0;
	const auto RejectAndCancel = [&bCancelAfterRejected, &RejectedCalls](uint32, uint64& Out) {
		++RejectedCalls;
		Out = 0; // A five-value range rejects zero, as its threshold is one.
		bCancelAfterRejected = true;
		return true;
	};
	const auto CancelAfterRejection = [&bCancelAfterRejected]() {
		return bCancelAfterRejected;
	};
	TestFalse(TEXT("cancellation after rejected draw"),
		TryUniformSigned(-2, 2, 0, 3, RejectAndCancel, CancelAfterRejection, Signed, Used));
	TestEqual(TEXT("one draw before cancellation"), RejectedCalls, uint32(1));
	TestEqual(TEXT("post-rejection cancellation leaves signed output"), Signed, int64(77));
	TestEqual(TEXT("post-rejection cancellation leaves count"), Used, uint32(99));
	return true;
}
#endif
