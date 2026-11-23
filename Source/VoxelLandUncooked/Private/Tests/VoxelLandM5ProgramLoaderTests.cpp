#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandM5ProgramLoader.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5ProgramLoaderFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandSerializedProgram M5ProgramLoaderPayload()
{
	FVoxelLandSerializedProgram Payload;
	Payload.ProgramVersion = int32(FVoxelLandProgramDraft::CurrentFormatVersion);
	Payload.CompilerSemanticVersion = int32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	Payload.RegisterCount = 1;
	// Independently encoded fixed-width LE streams, calculated with standalone
	// XXH3-64: empty source, then a current-format LoadZ/StoreDensity program.
	const uint64 SourceHash = 0x0c64ed733e702cc9ULL;
	const uint64 ProgramHash = 0xb934c14fd49b91c3ULL;
	FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
	FMemory::Memcpy(&Payload.ProgramFingerprint, &ProgramHash, sizeof(ProgramHash));
	FVoxelLandSerializedInstruction Load;
	Load.Opcode = int32(EVoxelLandOpcode::LoadZ);
	FVoxelLandSerializedInstruction Store;
	Store.Opcode = int32(EVoxelLandOpcode::StoreDensity);
	Payload.Instructions = {Load, Store};
	return Payload;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProgramLoaderCompatibilityTest,
	"VoxelLand.M5.Identity.ProgramLoader.Compatibility", M5ProgramLoaderFlags)
bool FVoxelLandM5ProgramLoaderCompatibilityTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this] {
		const FVoxelLandGraphSnapshot Source;
		{
			const FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Baseline;
			FString Error;
			if (!TestTrue(TEXT("current codec loads fixed payload"),
					FVoxelLandProgramCodec::Load(Source, Payload, Baseline, Error))
				|| !Baseline)
			{
				return false;
			}
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
			if (!FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Budget))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			const FVoxelLandM5Diagnostic D =
				FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), [] { return false; }, Program);
			if (!TestEqual(TEXT("metered load succeeds"), int32(D.Code), int32(EVoxelLandM5ErrorCode::None))
				|| !Program)
			{
				return false;
			}
			const uint64 Expected = 0xb934c14fd49b91c3ULL;
			TestEqual(TEXT("independent fingerprint"), Program->GetFingerprint(), Expected);
			TestEqual(TEXT("current codec fingerprint"), Program->GetFingerprint(), Baseline->GetFingerprint());
			TestEqual(TEXT("format preserved"), Program->GetData().FormatVersion,
				FVoxelLandProgramDraft::CurrentFormatVersion);
			int64 Live = 0, Peak = 0, Work = 0;
			Budget->GetUsage(Live, Peak, Work);
			TestTrue(TEXT("validated program retains admission"), Live > 0);
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Reader = Program;
			Program.Reset();
			Budget->GetUsage(Live, Peak, Work);
			TestTrue(TEXT("last reader keeps admission"), Live > 0);
			Reader.Reset();
			Budget->GetUsage(Live, Peak, Work);
			TestEqual(TEXT("last reader releases admission"), Live, int64(0));
		}
		return true;
	}).Get();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramLoaderFailuresTest, "VoxelLand.M5.Identity.ProgramLoader.Failures", M5ProgramLoaderFlags)
bool FVoxelLandM5ProgramLoaderFailuresTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this] {
		const FVoxelLandGraphSnapshot Source;
		FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Prior;
		FString Error;
		if (!FVoxelLandProgramCodec::Load(Source, Payload, Prior, Error) || !Prior)
		{
			return false;
		}
		const FVoxelLandProgram* Original = Prior.Get();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Budget);
		const auto NeverCancel = [] {
			return false;
		};
		Payload.ProgramFingerprint ^= 1;
		TestEqual(TEXT("corrupt program fingerprint"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("corrupt program retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload();
		Payload.SourceFingerprint ^= 1;
		TestEqual(TEXT("corrupt source fingerprint"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("corrupt source retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload();
		Payload.CompilerSemanticVersion += 1;
		TestEqual(TEXT("incompatible semantics"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
		Payload = M5ProgramLoaderPayload();
		Payload.ProgramVersion = 1;
		TestEqual(TEXT("old program format rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
		TestTrue(TEXT("old format retains prior"), Prior.Get() == Original);
		Payload.ProgramVersion = 3;
		TestEqual(TEXT("unknown program format rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
		TestTrue(TEXT("unknown format retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload();
		Payload.ExternalOperators.AddDefaulted();
		TestEqual(TEXT("external provider rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::Unproven));
		TestTrue(TEXT("provider rejection retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload();
		FVoxelLandGraphSnapshot FunctionSource;
		FunctionSource.Functions.AddDefaulted();
		TestEqual(TEXT("malformed function dependency rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(FunctionSource, Payload, Budget.ToSharedRef(), NeverCancel, Prior)
					.Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("function rejection retains prior"), Prior.Get() == Original);
		TestEqual(TEXT("immediate cancellation"),
			int32(FVoxelLandM5ProgramLoader::Load(
				Source, Payload, Budget.ToSharedRef(), [] { return true; }, Prior)
					.Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestTrue(TEXT("cancellation retains prior"), Prior.Get() == Original);
		int64 Live = 0, Peak = 0, Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("failed loads release scratch"), Live, int64(0));
		return true;
	}).Get();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProgramLoaderBudgetTest,
	"VoxelLand.M5.Identity.ProgramLoader.BudgetAndCancellation", M5ProgramLoaderFlags)
bool FVoxelLandM5ProgramLoaderBudgetTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this] {
		const FVoxelLandGraphSnapshot Source;
		const FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Probe;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Probe);
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Prior;
		int32 Polls = 0;
		const FVoxelLandM5Diagnostic First = FVoxelLandM5ProgramLoader::Load(
			Source, Payload, Probe.ToSharedRef(),
			[&Polls] {
				++Polls;
				return false;
			},
			Prior);
		if (!TestEqual(TEXT("probe succeeds"), int32(First.Code), int32(EVoxelLandM5ErrorCode::None)) || !Prior)
		{
			return false;
		}
		const FVoxelLandProgram* Original = Prior.Get();
		int64 Live = 0, Peak = 0, Work = 0;
		Probe->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("work and peak measured"), Work > 0 && Peak > 0);
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Insufficient;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, Work - 1, Insufficient);
		TestEqual(TEXT("one below cumulative work"),
			int32(FVoxelLandM5ProgramLoader::Load(
				Source, Payload, Insufficient.ToSharedRef(), [] { return false; }, Prior)
					.Code),
			int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("work exhaustion retains prior"), Prior.Get() == Original);
		Insufficient->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("work exhaustion releases scratch"), Live, int64(0));
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> CancelBudget;
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, CancelBudget);
		int32 CancelPolls = 0;
		const int32 CancelAt = Polls;
		TestEqual(TEXT("cancel at final publication"),
			int32(FVoxelLandM5ProgramLoader::Load(
				Source, Payload, CancelBudget.ToSharedRef(),
				[&CancelPolls, CancelAt] { return ++CancelPolls == CancelAt; }, Prior)
					.Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestEqual(TEXT("final poll reached"), CancelPolls, CancelAt);
		TestTrue(TEXT("late cancellation retains prior"), Prior.Get() == Original);
		CancelBudget->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("late cancellation releases program and draft"), Live, int64(0));
		return true;
	}).Get();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramLoaderV2CombinedTest, "VoxelLand.M5.Identity.ProgramLoader.V2Combined", M5ProgramLoaderFlags)
bool FVoxelLandM5ProgramLoaderV2CombinedTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this] {
		FVoxelLandGraphSnapshot Source;
		FVoxelLandGraphParameter Frequency;
		Frequency.Id = FGuid(1, 0, 0, 0);
		Frequency.Name = TEXT("Frequency");
		Frequency.FloatDefault = 0.25f;
		FVoxelLandGraphParameter Enabled;
		Enabled.Id = FGuid(2, 0, 0, 0);
		Enabled.Name = TEXT("Enabled");
		Enabled.Type = EVoxelLandGraphValueType::Bool;
		Enabled.BoolDefault = true;
		FVoxelLandGraphParameter Strength;
		Strength.Id = FGuid(3, 0, 0, 0);
		Strength.Name = TEXT("Strength");
		Strength.FloatDefault = 0.5f;
		Source.Parameters = {Frequency, Enabled, Strength};
		FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
		// Independently encoded canonical source (185 bytes) and program (210
		// bytes), calculated using a standalone XXH3-64 implementation.
		const uint64 SourceHash = 0x26922d3c2bd5b6ebULL;
		const uint64 ProgramHash = 0x59d92600632f8818ULL;
		FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
		FMemory::Memcpy(&Payload.ProgramFingerprint, &ProgramHash, sizeof(ProgramHash));
		Payload.Constants = {3.0f, 0.5f, 2.0f};
		FVoxelLandSerializedParameter FreqRecord;
		FreqRecord.Id = Frequency.Id;
		FreqRecord.Name = Frequency.Name;
		FreqRecord.FloatDefault = 0.25f;
		FVoxelLandSerializedParameter EnabledRecord;
		EnabledRecord.Id = Enabled.Id;
		EnabledRecord.Name = Enabled.Name;
		EnabledRecord.Type = uint8(EVoxelLandParameterType::Bool);
		EnabledRecord.IntDefault = 1;
		FVoxelLandSerializedParameter StrengthRecord;
		StrengthRecord.Id = Strength.Id;
		StrengthRecord.Name = Strength.Name;
		StrengthRecord.FloatDefault = 0.5f;
		Payload.Parameters = {FreqRecord, EnabledRecord, StrengthRecord};
		FVoxelLandSerializedSeed Seed;
		Seed.NodeId = FGuid(1, 0, 0, 0);
		Seed.Salt = 7;
		Payload.Seeds.Add(Seed);
		FVoxelLandSerializedNoise Noise;
		Noise.SeedSlot = 0;
		Noise.Frequency.bParameter = true;
		Noise.Frequency.Index = 0;
		Noise.Octaves.Index = 0;
		Noise.Gain.Index = 1;
		Noise.Lacunarity.Index = 2;
		Payload.Noise.Add(Noise);
		Payload.SmoothFilter.bPresent = true;
		Payload.SmoothFilter.Enabled.bParameter = true;
		Payload.SmoothFilter.Enabled.Index = 1;
		Payload.SmoothFilter.Strength.bParameter = true;
		Payload.SmoothFilter.Strength.Index = 2;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		const FVoxelLandM5Diagnostic D =
			FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), [] { return false; }, Program);
		if (!TestEqual(TEXT("v2 combined load"), int32(D.Code), int32(EVoxelLandM5ErrorCode::None)) || !Program)
		{
			return false;
		}
		TestEqual(TEXT("fixed program fingerprint"), Program->GetFingerprint(), ProgramHash);
		const FVoxelLandProgramDraft& Data = Program->GetData();
		if (!TestEqual(TEXT("three parameters"), Data.Parameters.Num(), 3)
			|| !TestEqual(TEXT("three constants"), Data.Constants.Num(), 3)
			|| !TestEqual(TEXT("one seed"), Data.Seeds.Num(), 1) || !TestEqual(TEXT("one noise"), Data.Noise.Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("frequency name normalized"), Data.Parameters[0].Name, FString(TEXT("frequency")));
		TestEqual(TEXT("frequency default"), Data.Parameters[0].FloatDefault, 0.25f);
		TestEqual(TEXT("enabled type"), int32(Data.Parameters[1].Type), int32(EVoxelLandParameterType::Bool));
		TestEqual(TEXT("enabled default"), Data.Parameters[1].IntDefault, 1);
		TestEqual(TEXT("strength default"), Data.Parameters[2].FloatDefault, 0.5f);
		TestEqual(TEXT("octaves constant"), Data.Constants[0], 3.0f);
		TestEqual(TEXT("gain constant"), Data.Constants[1], 0.5f);
		TestEqual(TEXT("lacunarity constant"), Data.Constants[2], 2.0f);
		TestTrue(TEXT("seed GUID"), Data.Seeds[0].NodeId == Seed.NodeId);
		TestEqual(TEXT("seed salt"), Data.Seeds[0].Salt, uint32(7));
		const FVoxelLandNoiseConfig& DecodedNoise = Data.Noise[0];
		TestEqual(TEXT("noise seed slot"), DecodedNoise.SeedSlot, uint32(0));
		TestTrue(TEXT("frequency parameter"), DecodedNoise.Frequency.bParameter);
		TestEqual(TEXT("frequency index"), DecodedNoise.Frequency.Index, uint32(0));
		TestFalse(TEXT("octaves constant"), DecodedNoise.Octaves.bParameter);
		TestEqual(TEXT("octaves index"), DecodedNoise.Octaves.Index, uint32(0));
		TestFalse(TEXT("gain constant"), DecodedNoise.Gain.bParameter);
		TestEqual(TEXT("gain index"), DecodedNoise.Gain.Index, uint32(1));
		TestFalse(TEXT("lacunarity constant"), DecodedNoise.Lacunarity.bParameter);
		TestEqual(TEXT("lacunarity index"), DecodedNoise.Lacunarity.Index, uint32(2));
		TestTrue(TEXT("smooth filter present"), Data.SmoothFilter.bPresent);
		TestTrue(TEXT("smooth enabled parameter"), Data.SmoothFilter.Enabled.bParameter);
		TestEqual(TEXT("smooth enabled index"), Data.SmoothFilter.Enabled.Index, uint32(1));
		TestTrue(TEXT("smooth strength parameter"), Data.SmoothFilter.Strength.bParameter);
		TestEqual(TEXT("smooth strength index"), Data.SmoothFilter.Strength.Index, uint32(2));
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Baseline;
		FString Error;
		if (!TestTrue(TEXT("current codec accepts same v2 payload"),
				FVoxelLandProgramCodec::Load(Source, Payload, Baseline, Error))
			|| !Baseline)
		{
			return false;
		}
		TestEqual(TEXT("current codec fingerprint unchanged"), Baseline->GetFingerprint(), ProgramHash);
		return true;
	}).Get();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5ProgramLoaderFunctionsTest, "VoxelLand.M5.Identity.ProgramLoader.Functions", M5ProgramLoaderFlags)
bool FVoxelLandM5ProgramLoaderFunctionsTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this] {
		FVoxelLandGraphSnapshot Source;
		FVoxelLandGraphNodeRecord ACall;
		ACall.Id = FGuid(1, 0, 0, 0);
		ACall.OperatorId = VoxelLandGraphFunctionCallId();
		ACall.FunctionPath = FSoftObjectPath(TEXT("/Game/A.A"));
		FVoxelLandGraphNodeRecord BCall = ACall;
		BCall.Id = FGuid(2, 0, 0, 0);
		BCall.FunctionPath = FSoftObjectPath(TEXT("/Game/B.B"));
		Source.Nodes = {ACall, BCall};
		FVoxelLandGraphFunctionSnapshot A;
		A.AssetPath = TEXT("/Game/A.A");
		FVoxelLandGraphNodeRecord Nested = BCall;
		Nested.Id = FGuid(10, 0, 0, 0);
		A.Nodes.Add(Nested);
		FVoxelLandGraphFunctionSnapshot B;
		B.AssetPath = TEXT("/Game/B.B");
		Source.Functions = {A, B};
		// Independently encoded 342-byte graph/function extension from the
		// source-fingerprint contract, including A -> B and a shared root B.
		const uint64 SourceHash = 0xf768e5271f7fded7ULL;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 200000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		const auto NeverCancel = [] {
			return false;
		};
		{
			FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
			FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
			const FVoxelLandM5Diagnostic D =
				FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Program);
			if (!TestEqual(
					TEXT("nested shared zero-input functions load"), int32(D.Code), int32(EVoxelLandM5ErrorCode::None))
				|| !Program)
			{
				return false;
			}
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Baseline;
			FString Error;
			if (!TestTrue(TEXT("current codec accepts captured dependencies"),
					FVoxelLandProgramCodec::Load(Source, Payload, Baseline, Error))
				|| !Baseline)
			{
				return false;
			}
			const uint64 Expected = 0xb934c14fd49b91c3ULL;
			TestEqual(TEXT("fixed program identity"), Program->GetFingerprint(), Expected);
			TestEqual(TEXT("current codec program identity"), Baseline->GetFingerprint(), Expected);
		}
		const FVoxelLandProgram* Previous = Program.Get();
		FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload();
		FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
		FVoxelLandGraphSnapshot Missing = Source;
		Missing.Functions.RemoveAt(1);
		TestEqual(TEXT("missing dependency rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Missing, Payload, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("missing preserves program"), Program.Get() == Previous);
		FVoxelLandGraphSnapshot Cycle = Source;
		Cycle.Functions[1].Nodes.Add(ACall);
		TestEqual(TEXT("function cycle rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Cycle, Payload, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("cycle preserves program"), Program.Get() == Previous);
		FVoxelLandGraphSnapshot InvalidVersion = Source;
		InvalidVersion.Functions[0].SourceVersion = 2;
		TestEqual(TEXT("function source version rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(InvalidVersion, Payload, Budget.ToSharedRef(), NeverCancel, Program)
					.Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		FVoxelLandGraphSnapshot TooManyPins = Source;
		TooManyPins.Nodes[0].Pins.SetNum(65);
		TestEqual(TEXT("legacy pin limit enforced"),
			int32(
				FVoxelLandM5ProgramLoader::Load(TooManyPins, Payload, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		FVoxelLandGraphSnapshot InputInterface = Source;
		FVoxelLandGraphParameter& Input = InputInterface.Functions[0].Inputs.AddDefaulted_GetRef();
		Input.Id = FGuid(44, 1, 2, 3);
		Input.Name = TEXT("amplitude");
		Input.FloatDefault = 2.5f;
		Input.Tooltip = FText::FromString(TEXT("authoring text excluded from the fingerprint"));
		const uint64 InputHash = FVoxelLandProgramCodec::SourceFingerprint(InputInterface);
		FMemory::Memcpy(&Payload.SourceFingerprint, &InputHash, sizeof(InputHash));
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> InputBudget;
		if (!FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 200000000, InputBudget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> InputProgram;
		TestEqual(TEXT("parameterized function loads"),
			int32(FVoxelLandM5ProgramLoader::Load(InputInterface, Payload, InputBudget.ToSharedRef(), NeverCancel,
				InputProgram).Code), int32(EVoxelLandM5ErrorCode::None));
		if (!TestTrue(TEXT("parameterized function yields program"), InputProgram.IsValid()))
		{
			return false;
		}
		Input.Tooltip = FText::FromString(TEXT("different presentation text"));
		TestEqual(TEXT("tooltip changes do not invalidate compiled input"),
			int32(FVoxelLandM5ProgramLoader::Load(InputInterface, Payload, InputBudget.ToSharedRef(), NeverCancel,
				InputProgram).Code), int32(EVoxelLandM5ErrorCode::None));
		const FVoxelLandProgram* PreviousInputProgram = InputProgram.Get();
		Input.FloatDefault = 3.5f;
		TestEqual(TEXT("changed function input rejects stale program"),
			int32(FVoxelLandM5ProgramLoader::Load(InputInterface, Payload, InputBudget.ToSharedRef(), NeverCancel,
				InputProgram).Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("stale interface preserves program"), InputProgram.Get() == PreviousInputProgram);
		FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWork;
		FVoxelLandM5Budget::TryCreate(1024 * 1024, 1, NoWork);
		TestEqual(TEXT("dependency work exhausted"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, NoWork.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("budget failure preserves program"), Program.Get() == Previous);
		int32 Polls = 0;
		TestEqual(TEXT("function preflight cancellation"),
			int32(FVoxelLandM5ProgramLoader::Load(
				Source, Payload, Budget.ToSharedRef(), [&Polls] { return ++Polls == 3; }, Program)
					.Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestTrue(TEXT("cancellation preserves program"), Program.Get() == Previous);
		return true;
	}).Get();
}
#endif
