#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandM5DecodedDraft.h"
#include "Graph/VoxelLandM5ProgramDecoder.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5ProgramDecoderTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandSerializedProgram M5DecoderEmptyGraphPayload()
{
	FVoxelLandSerializedProgram Payload;
	Payload.ProgramVersion = 1;
	Payload.RegisterCount = 1;
	const uint64 SourceHash = 0xfe4f399ebe726e73ULL; // Independent canonical empty-source vector.
	FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
	const uint64 ProgramHash = 0x123456789abcdef0ULL; // Retained, not verified by the decoder.
	FMemory::Memcpy(&Payload.ProgramFingerprint, &ProgramHash, sizeof(ProgramHash));
	FVoxelLandSerializedInstruction Load;
	Load.Opcode = int32(EVoxelLandOpcode::LoadX);
	Payload.Instructions.Add(Load);
	FVoxelLandSerializedInstruction Store;
	Store.Opcode = int32(EVoxelLandOpcode::StoreDensity);
	Payload.Instructions.Add(Store);
	return Payload;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramDecoderBasicTest, "VoxelLand.M5.Identity.ProgramDecoder.Basic", M5ProgramDecoderTestFlags)
bool FVoxelLandM5ProgramDecoderBasicTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 100000, Budget)))
	{
		return false;
	}
	const FVoxelLandGraphSnapshot Source;
	const FVoxelLandSerializedProgram Payload = M5DecoderEmptyGraphPayload();
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result;
	const FVoxelLandM5Diagnostic D =
		FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), [] { return false; }, Result);
	TestEqual(TEXT("valid decode"), int32(D.Code), int32(EVoxelLandM5ErrorCode::None));
	if (!TestTrue(TEXT("result available"), Result.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("format"), Result->GetDraft().FormatVersion, uint32(1));
	TestEqual(TEXT("registers"), Result->GetDraft().RegisterCount, 1);
	TestEqual(TEXT("instructions"), Result->GetDraft().Instructions.Num(), 2);
	if (Result->GetDraft().Instructions.Num() == 2)
	{
		TestEqual(TEXT("load opcode"), int32(Result->GetDraft().Instructions[0].Op), int32(EVoxelLandOpcode::LoadX));
		TestEqual(
			TEXT("store opcode"), int32(Result->GetDraft().Instructions[1].Op), int32(EVoxelLandOpcode::StoreDensity));
	}
	TestEqual(TEXT("unverified program fingerprint retained"), Result->GetExpectedProgramFingerprint(),
		uint64(0x123456789abcdef0ULL));
	TestTrue(TEXT("origin budget retained"), Result->UsesBudget(*Budget));
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("result retains admitted memory"), Live > 0);
	const auto SecondReader = Result;
	Result.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("last reader retains memory"), Live > 0);
	// The decoded record is deliberately not an executable FVoxelLandProgram.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramDecoderFailureTest, "VoxelLand.M5.Identity.ProgramDecoder.Failures", M5ProgramDecoderTestFlags)
bool FVoxelLandM5ProgramDecoderFailureTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 100000, Budget)))
	{
		return false;
	}
	const FVoxelLandGraphSnapshot Source;
	FVoxelLandSerializedProgram Payload = M5DecoderEmptyGraphPayload();
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result;
	const auto NeverCancel = [] {
		return false;
	};
	if (!TestEqual(TEXT("seed result"),
			int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
			int32(EVoxelLandM5ErrorCode::None))
		|| !Result.IsValid())
	{
		return false;
	}
	const auto* Previous = Result.Get();
	Payload.Instructions[0].A = -1;
	TestEqual(TEXT("invalid register rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestTrue(TEXT("invalid register retains prior output"), Result.Get() == Previous);
	Payload = M5DecoderEmptyGraphPayload();
	Payload.SourceFingerprint = 7;
	TestEqual(TEXT("stale source rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestTrue(TEXT("stale source retains prior output"), Result.Get() == Previous);
	Payload = M5DecoderEmptyGraphPayload();
	Payload.Instructions[0].Opcode = int32(EVoxelLandOpcode::ExternalOperator);
	TestEqual(TEXT("external opcode rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TestTrue(TEXT("external opcode retains prior output"), Result.Get() == Previous);
	Payload = M5DecoderEmptyGraphPayload();
	Payload.EnvelopeVersion = 2;
	TestEqual(TEXT("unknown envelope rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
	TestTrue(TEXT("unknown envelope retains prior output"), Result.Get() == Previous);
	Payload = M5DecoderEmptyGraphPayload();
	int32 Polls = 0;
	TestEqual(TEXT("partial cancellation"),
		int32(FVoxelLandM5ProgramDecoder::Decode(
			Source, Payload, Budget.ToSharedRef(), [&Polls] { return ++Polls == 5; }, Result)
				.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancellation reached fingerprint"), Polls, 5);
	TestTrue(TEXT("cancellation retains prior output"), Result.Get() == Previous);

	FVoxelLandGraphSnapshot ParameterSource;
	FVoxelLandGraphParameter SourceParameter;
	SourceParameter.Id = FGuid(1, 2, 3, 4);
	SourceParameter.Name = TEXT("A");
	ParameterSource.Parameters.Add(SourceParameter);
	Payload = M5DecoderEmptyGraphPayload();
	const uint64 ParameterSourceHash = 0xd368c79780f36f3aULL; // Independent canonical source vector.
	FMemory::Memcpy(&Payload.SourceFingerprint, &ParameterSourceHash, sizeof(ParameterSourceHash));
	FVoxelLandSerializedParameter Parameter;
	Parameter.Id = SourceParameter.Id;
	Parameter.Name = TEXT("a");
	Payload.Parameters.Add(Parameter);
	TestEqual(TEXT("case-insensitive parameter mapping"),
		int32(FVoxelLandM5ProgramDecoder::Decode(ParameterSource, Payload, Budget.ToSharedRef(), NeverCancel, Result)
				.Code),
		int32(EVoxelLandM5ErrorCode::None));
	if (!TestTrue(TEXT("parameter result available"), Result.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("parameter count"), Result->GetDraft().Parameters.Num(), 1);
	Previous = Result.Get();
	Payload.Parameters.Add(Parameter);
	TestEqual(TEXT("duplicate parameter mapping rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(ParameterSource, Payload, Budget.ToSharedRef(), NeverCancel, Result)
				.Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestTrue(TEXT("duplicate retains prior output"), Result.Get() == Previous);
	Payload.Parameters.RemoveAt(1);
	Payload.Parameters[0].Type = 1;
	TestEqual(TEXT("parameter type mismatch rejected"),
		int32(FVoxelLandM5ProgramDecoder::Decode(ParameterSource, Payload, Budget.ToSharedRef(), NeverCancel, Result)
				.Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestTrue(TEXT("type mismatch retains prior output"), Result.Get() == Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramDecoderBudgetTest, "VoxelLand.M5.Identity.ProgramDecoder.Budget", M5ProgramDecoderTestFlags)
bool FVoxelLandM5ProgramDecoderBudgetTest::RunTest(const FString& Parameters)
{
	const FVoxelLandGraphSnapshot Source;
	const FVoxelLandSerializedProgram Payload = M5DecoderEmptyGraphPayload();
	// Empty source costs 85 work; two decoded instructions cost 80. Fingerprint
	// scratch (4116) and decoded result (4096+8*sizeof(instruction)) do not overlap.
	const int64 DraftBytes = 4096 + 8 * int64(sizeof(FVoxelLandInstruction));
	const int64 Memory = FMath::Max<int64>(4116, DraftBytes);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Exact;
	FVoxelLandM5Budget::TryCreate(Memory, 165, Exact);
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result;
	const auto NeverCancel = [] {
		return false;
	};
	TestEqual(TEXT("exact admission"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Exact.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	if (!TestTrue(TEXT("exact result"), Result.IsValid()))
	{
		return false;
	}
	const auto* Previous = Result.Get();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ShortWork;
	FVoxelLandM5Budget::TryCreate(Memory, 164, ShortWork);
	TestEqual(TEXT("one work below"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, ShortWork.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestTrue(TEXT("work failure retains output"), Result.Get() == Previous);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ShortMemory;
	FVoxelLandM5Budget::TryCreate(Memory - 1, 165, ShortMemory);
	TestEqual(TEXT("one memory below"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, ShortMemory.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestTrue(TEXT("memory failure retains output"), Result.Get() == Previous);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProgramDecoderVersionTwoTest,
	"VoxelLand.M5.Identity.ProgramDecoder.VersionTwo", M5ProgramDecoderTestFlags)
bool FVoxelLandM5ProgramDecoderVersionTwoTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 100000, Budget)))
	{
		return false;
	}
	FVoxelLandGraphSnapshot Source;
	FVoxelLandSerializedProgram Payload = M5DecoderEmptyGraphPayload();
	Payload.ProgramVersion = 2;
	Payload.CompilerSemanticVersion = int32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	Payload.Instructions[0].Opcode = int32(EVoxelLandOpcode::DynamicSphere);
	Payload.Instructions[0].Dst = 1;
	Payload.Instructions[0].A = 2;
	Payload.Instructions[0].B = 3;
	Payload.Instructions[0].C = 4;
	Payload.Instructions[0].D = 5;
	Payload.Instructions[0].E = 6;
	Payload.Instructions[0].F = 7;
	Payload.Instructions[0].Immediate = -1;
	Payload.Constants = {3.5f, -2.0f};
	FVoxelLandSerializedSeed Seed;
	Seed.NodeId = FGuid(1, 2, 3, 4);
	Seed.Salt = -2;
	Payload.Seeds.Add(Seed);
	FVoxelLandSerializedNoise Noise;
	Noise.SeedSlot = -1;
	Noise.Frequency.bParameter = true;
	Noise.Frequency.Index = 3;
	Noise.Octaves.Index = -2;
	Noise.Gain.bParameter = true;
	Noise.Gain.Index = 7;
	Noise.Lacunarity.Index = 9;
	Payload.Noise.Add(Noise);
	Payload.SmoothFilter.bPresent = true;
	Payload.SmoothFilter.Enabled.bParameter = true;
	Payload.SmoothFilter.Enabled.Index = 4;
	Payload.SmoothFilter.Strength.Index = 5;
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result;
	const auto NeverCancel = [] {
		return false;
	};
	TestEqual(TEXT("v2 decode"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	if (!TestTrue(TEXT("v2 draft exists"), Result.IsValid()))
	{
		return false;
	}
	const FVoxelLandProgramDraft& Draft = Result->GetDraft();
	TestEqual(TEXT("v2 format"), Draft.FormatVersion, uint32(2));
	if (!TestEqual(TEXT("instruction count"), Draft.Instructions.Num(), 2)
		|| !TestEqual(TEXT("constant count"), Draft.Constants.Num(), 2)
		|| !TestEqual(TEXT("seed count"), Draft.Seeds.Num(), 1)
		|| !TestEqual(TEXT("noise count"), Draft.Noise.Num(), 1))
	{
		return false;
	}
	const FVoxelLandInstruction& I = Draft.Instructions[0];
	TestEqual(TEXT("v2 opcode"), int32(I.Op), int32(EVoxelLandOpcode::DynamicSphere));
	TestEqual(TEXT("dst"), I.Dst, uint16(1));
	TestEqual(TEXT("a"), I.A, uint16(2));
	TestEqual(TEXT("b"), I.B, uint16(3));
	TestEqual(TEXT("c"), I.C, uint16(4));
	TestEqual(TEXT("d"), I.D, uint16(5));
	TestEqual(TEXT("e"), I.E, uint16(6));
	TestEqual(TEXT("f"), I.F, uint16(7));
	TestEqual(TEXT("immediate bit pattern"), I.Imm, MAX_uint32);
	TestEqual(TEXT("constant 0"), Draft.Constants[0], 3.5f);
	TestEqual(TEXT("constant 1"), Draft.Constants[1], -2.0f);
	TestTrue(TEXT("seed node"), Draft.Seeds[0].NodeId == Seed.NodeId);
	TestEqual(TEXT("seed salt bits"), Draft.Seeds[0].Salt, uint32(MAX_uint32 - 1));
	TestEqual(TEXT("noise seed bits"), Draft.Noise[0].SeedSlot, MAX_uint32);
	TestTrue(TEXT("frequency parameter flag"), Draft.Noise[0].Frequency.bParameter);
	TestEqual(TEXT("frequency index"), Draft.Noise[0].Frequency.Index, uint32(3));
	TestEqual(TEXT("octaves bits"), Draft.Noise[0].Octaves.Index, uint32(MAX_uint32 - 1));
	TestTrue(TEXT("gain parameter flag"), Draft.Noise[0].Gain.bParameter);
	TestEqual(TEXT("gain index"), Draft.Noise[0].Gain.Index, uint32(7));
	TestEqual(TEXT("lacunarity index"), Draft.Noise[0].Lacunarity.Index, uint32(9));
	TestTrue(TEXT("smooth filter present"), Draft.SmoothFilter.bPresent);
	TestTrue(TEXT("smooth enabled parameter flag"), Draft.SmoothFilter.Enabled.bParameter);
	TestEqual(TEXT("smooth enabled index"), Draft.SmoothFilter.Enabled.Index, uint32(4));
	TestEqual(TEXT("smooth strength index"), Draft.SmoothFilter.Strength.Index, uint32(5));
	// The fixture intentionally has invalid register/source references: the
	// decoder preserves them and must not be treated as program validation.
	const auto* Previous = Result.Get();
	Payload.CompilerSemanticVersion += 1;
	TestEqual(TEXT("incompatible v2 semantic version"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
	TestTrue(TEXT("version error retains output"), Result.Get() == Previous);
	Payload.CompilerSemanticVersion = int32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	Source.Functions.AddDefaulted();
	TestEqual(TEXT("function dependencies unsupported"),
		int32(FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TestTrue(TEXT("function rejection retains output"), Result.Get() == Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProgramDecoderPartialCancelTest,
	"VoxelLand.M5.Identity.ProgramDecoder.PartialCancel", M5ProgramDecoderTestFlags)
bool FVoxelLandM5ProgramDecoderPartialCancelTest::RunTest(const FString& Parameters)
{
	const FVoxelLandGraphSnapshot Source;
	const FVoxelLandSerializedProgram Payload = M5DecoderEmptyGraphPayload();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> PreviousBudget;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(8192, 1000, PreviousBudget)
		|| !FVoxelLandM5Budget::TryCreate(8192, 1000, Budget))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result;
	if (!TestEqual(TEXT("seed previous"),
			int32(FVoxelLandM5ProgramDecoder::Decode(
				Source, Payload, PreviousBudget.ToSharedRef(), [] { return false; }, Result)
					.Code),
			int32(EVoxelLandM5ErrorCode::None))
		|| !Result.IsValid())
	{
		return false;
	}
	const auto* Previous = Result.Get();
	int32 Polls = 0;
	// Poll 5 enters instruction 0 after allocation; poll 6 occurs before
	// instruction 1, after one decoded instruction is held in local scratch.
	const FVoxelLandM5Diagnostic D = FVoxelLandM5ProgramDecoder::Decode(
		Source, Payload, Budget.ToSharedRef(), [&Polls] { return ++Polls == 6; }, Result);
	TestEqual(TEXT("cancel during partial decode"), int32(D.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("partial decode poll"), Polls, 6);
	TestTrue(TEXT("prior result preserved"), Result.Get() == Previous);
	int64 Live = -1, Peak = -1, Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("partial draft memory released"), Live, int64(0));
	TestTrue(TEXT("result allocation occurred"), Peak > 4116);
	return true;
}
#endif
