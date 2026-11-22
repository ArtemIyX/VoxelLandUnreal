#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
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

FVoxelLandSerializedProgram M5ProgramLoaderPayload(int32 Version)
{
	FVoxelLandSerializedProgram Payload;
	Payload.ProgramVersion = Version;
	Payload.CompilerSemanticVersion = Version == 2 ? int32(FVoxelLandProgramDraft::CompilerSemanticVersion) : 0;
	Payload.RegisterCount = 1;
	// Independently encoded fixed-width LE streams, calculated with standalone
	// xxHash64: empty source, then LoadZ/StoreDensity program in each version.
	const uint64 SourceHash = 0xfe4f399ebe726e73ULL;
	const uint64 ProgramHash = Version == 2 ? 0x65dfaa0518ea24e0ULL : 0x4412893f00431bf7ULL;
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
		for (int32 Version : {1, 2})
		{
			const FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload(Version);
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Legacy;
			FString Error;
			if (!TestTrue(TEXT("legacy codec loads fixed payload"),
					FVoxelLandProgramCodec::Load(Source, Payload, Legacy, Error))
				|| !Legacy)
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
			const uint64 Expected = Version == 2 ? 0x65dfaa0518ea24e0ULL : 0x4412893f00431bf7ULL;
			TestEqual(TEXT("independent fingerprint"), Program->GetFingerprint(), Expected);
			TestEqual(TEXT("legacy fingerprint"), Program->GetFingerprint(), Legacy->GetFingerprint());
			TestEqual(TEXT("format preserved"), Program->GetData().FormatVersion, uint32(Version));
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
		FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload(2);
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
		Payload = M5ProgramLoaderPayload(2);
		Payload.SourceFingerprint ^= 1;
		TestEqual(TEXT("corrupt source fingerprint"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("corrupt source retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload(2);
		Payload.CompilerSemanticVersion += 1;
		TestEqual(TEXT("incompatible semantics"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::UnsupportedVersion));
		Payload = M5ProgramLoaderPayload(2);
		Payload.ExternalOperators.AddDefaulted();
		TestEqual(TEXT("external provider rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(Source, Payload, Budget.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::Unproven));
		TestTrue(TEXT("provider rejection retains prior"), Prior.Get() == Original);
		Payload = M5ProgramLoaderPayload(2);
		FVoxelLandGraphSnapshot FunctionSource;
		FunctionSource.Functions.AddDefaulted();
		TestEqual(TEXT("function dependency rejected"),
			int32(FVoxelLandM5ProgramLoader::Load(FunctionSource, Payload, Budget.ToSharedRef(), NeverCancel, Prior)
					.Code),
			int32(EVoxelLandM5ErrorCode::Unproven));
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
		const FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload(1);
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
		FVoxelLandSerializedProgram Payload = M5ProgramLoaderPayload(2);
		// Independently encoded canonical source (185 bytes) and program (210
		// bytes), calculated using a standalone xxHash64 implementation.
		const uint64 SourceHash = 0xf9ef65dcbe6e915dULL;
		const uint64 ProgramHash = 0x0dbfcdc17c797c29ULL;
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
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Legacy;
		FString Error;
		if (!TestTrue(TEXT("legacy codec accepts same v2 payload"),
				FVoxelLandProgramCodec::Load(Source, Payload, Legacy, Error))
			|| !Legacy)
		{
			return false;
		}
		TestEqual(TEXT("legacy fingerprint unchanged"), Legacy->GetFingerprint(), ProgramHash);
		return true;
	}).Get();
}
#endif
