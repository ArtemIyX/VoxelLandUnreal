#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5MeteredProgramFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandProgramDraft M5MeteredProgramSimpleDraft()
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = FVoxelLandProgramDraft::CurrentFormatVersion;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadZ;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Store.A = 0;
	Draft.Instructions = {Load, Store};
	return Draft;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MeteredProgramTest, "VoxelLand.M5.Program.MeteredValidation", M5MeteredProgramFlags)
bool FVoxelLandM5MeteredProgramTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this]() {
		FVoxelLandProgramDraft Draft = M5MeteredProgramSimpleDraft();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Budget)))
		{
			return false;
		}
		auto NeverCancel = [] {
			return false;
		};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Legacy;
		const FVoxelLandError LegacyError = FVoxelLandProgram::Validate(Draft, Legacy);
		if (!TestFalse(TEXT("legacy program validates"), bool(LegacyError)) || !Legacy)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		const FVoxelLandM5Diagnostic Success =
			FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), NeverCancel, Program);
		if (!TestEqual(TEXT("metered program validates"), int32(Success.Code), int32(EVoxelLandM5ErrorCode::None))
			|| !Program)
		{
			return false;
		}
		// Independently encoded current program with compiler semantic version,
		// full-width instructions and an empty external-operator table.
		const uint8 Bytes[] = {2, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0};
		const uint64 Expected = FXxHash64::HashBuffer(Bytes, sizeof(Bytes)).Hash;
		TestEqual(TEXT("independent canonical fingerprint"), Program->GetFingerprint(), Expected);
		TestEqual(TEXT("legacy fingerprint preserved"), Program->GetFingerprint(), Legacy->GetFingerprint());
		int64 Live = 0, Peak = 0, Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("result retains a memory lease"), Live > 0);
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Retained = Program;
		Program.Reset();
		Budget->GetUsage(Live, Peak, Work);
		TestTrue(TEXT("lease remains with a second reader"), Live > 0);
		Retained.Reset();
		Budget->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("last reader releases lease"), Live, int64(0));
		return true;
	}).Get();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MeteredProgramFailureTest, "VoxelLand.M5.Program.MeteredFailures", M5MeteredProgramFlags)
bool FVoxelLandM5MeteredProgramFailureTest::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this]() {
		FVoxelLandProgramDraft Draft = M5MeteredProgramSimpleDraft();
		auto NeverCancel = [] {
			return false;
		};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Prior;
		if (FVoxelLandProgram::Validate(Draft, Prior) || !Prior)
		{
			AddError(TEXT("Could not prepare the retained legacy program"));
			return false;
		}
		const FVoxelLandProgram* Original = Prior.Get();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SmallWork, SmallBytes, Sufficient;
		FVoxelLandM5Budget::TryCreate(1024 * 1024, 1, SmallWork);
		FVoxelLandM5Budget::TryCreate(1, 10000000, SmallBytes);
		FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 10000000, Sufficient);
		FVoxelLandProgramDraft Obsolete = Draft;
		Obsolete.FormatVersion = 1;
		TestEqual(TEXT("obsolete program format rejected"),
			int32(FVoxelLandProgram::ValidateMetered(Obsolete, Sufficient.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("obsolete program preserves previous output"), Prior.Get() == Original);
		TestEqual(TEXT("work exhaustion"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, SmallWork.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("work failure preserves previous output"), Prior.Get() == Original);
		TestEqual(TEXT("memory exhaustion"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, SmallBytes.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("memory failure preserves previous output"), Prior.Get() == Original);
		int32 Polls = 0;
		auto CancelPartway = [&Polls] {
			return ++Polls == 6;
		};
		TestEqual(TEXT("cancel after validation begins"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, Sufficient.ToSharedRef(), CancelPartway, Prior).Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestTrue(TEXT("cancellation preserves previous output"), Prior.Get() == Original);
		FVoxelLandExternalOperator Provider;
		Draft.ExternalOperators.Add(Provider);
		TestEqual(TEXT("external provider rejected before callback"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, Sufficient.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::ProviderUnavailable));
		TestTrue(TEXT("unsupported provider preserves output"), Prior.Get() == Original);
		Draft.ExternalOperators.Reset();
		Draft.Instructions[1].A = 3;
		TestEqual(TEXT("invalid register rejected"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, Sufficient.ToSharedRef(), NeverCancel, Prior).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("invalid program preserves output"), Prior.Get() == Original);
		int64 Live = 0, Peak = 0, Work = 0;
		Sufficient->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("failed attempts release memory"), Live, int64(0));
		return true;
	}).Get();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MeteredProgramV2Test, "VoxelLand.M5.Program.MeteredV2AndCancellation", M5MeteredProgramFlags)
bool FVoxelLandM5MeteredProgramV2Test::RunTest(const FString& Parameters)
{
	return Async(EAsyncExecution::ThreadPool, [this]() {
		FVoxelLandProgramDraft Draft = M5MeteredProgramSimpleDraft();
		Draft.FormatVersion = 2;
		Draft.Constants = {3.0f, 0.5f, 2.0f};
		FVoxelLandParameterSlot Frequency;
		Frequency.Name = TEXT("Frequency");
		Frequency.Type = EVoxelLandParameterType::Float;
		Frequency.FloatDefault = 0.25f;
		FVoxelLandParameterSlot Enabled;
		Enabled.Name = TEXT("Enabled");
		Enabled.Type = EVoxelLandParameterType::Bool;
		Enabled.IntDefault = 1;
		FVoxelLandParameterSlot Strength;
		Strength.Name = TEXT("Strength");
		Strength.Type = EVoxelLandParameterType::Float;
		Strength.FloatDefault = 0.5f;
		Draft.Parameters = {Frequency, Enabled, Strength};
		FVoxelLandSeedSlot SeedA;
		SeedA.NodeId = FGuid(1, 0, 0, 0);
		SeedA.Salt = 7;
		FVoxelLandSeedSlot SeedB = SeedA;
		SeedB.NodeId = FGuid(2, 0, 0, 0);
		Draft.Seeds = {SeedA, SeedB};
		FVoxelLandNoiseConfig Noise;
		Noise.SeedSlot = 0;
		Noise.Frequency.bParameter = true;
		Noise.Frequency.Index = 0;
		Noise.Octaves.Index = 0;
		Noise.Gain.Index = 1;
		Noise.Lacunarity.Index = 2;
		Draft.Noise.Add(Noise);
		Draft.SmoothFilter.bPresent = true;
		Draft.SmoothFilter.Enabled.bParameter = true;
		Draft.SmoothFilter.Enabled.Index = 1;
		Draft.SmoothFilter.Strength.bParameter = true;
		Draft.SmoothFilter.Strength.Index = 2;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, 50000000, Budget)))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		int32 SuccessfulPolls = 0;
		auto CountOnly = [&SuccessfulPolls] {
			++SuccessfulPolls;
			return false;
		};
		const FVoxelLandM5Diagnostic Success =
			FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), CountOnly, Program);
		if (!TestEqual(TEXT("v2 noise and smooth validate"), int32(Success.Code), int32(EVoxelLandM5ErrorCode::None))
			|| !Program)
		{
			return false;
		}
		// 230 independently encoded little-endian bytes: v2/compiler versions,
		// operands, three normalized parameters, two seeds, noise sources and
		// smooth sources. This is not encoded by FCanonicalWriter in the test.
		const uint8 Canonical[] = {2, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 64,
			64, 0, 0, 0, 63, 0, 0, 0, 64, 3, 0, 0, 0, 9, 0, 0, 0, 102, 114, 101, 113, 117, 101, 110, 99, 121, 0, 0, 0,
			128, 62, 0, 0, 0, 0, 7, 0, 0, 0, 101, 110, 97, 98, 108, 101, 100, 2, 0, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0,
			115, 116, 114, 101, 110, 103, 116, 104, 0, 0, 0, 0, 63, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 1, 0, 0, 0, 0,
			0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 255, 1, 1, 1, 0, 0, 0,
			1, 2, 0, 0, 0, 0, 0, 0, 0};
		TestEqual(TEXT("v2 canonical bytes"), Program->GetFingerprint(),
			FXxHash64::HashBuffer(Canonical, sizeof(Canonical)).Hash);
		const FVoxelLandProgram* Prior = Program.Get();
		FVoxelLandProgramDraft Invalid = Draft;
		Invalid.Parameters[0].Name = TEXT("bad\u00e9");
		auto NeverCancel = [] {
			return false;
		};
		TestEqual(TEXT("non-ASCII name"),
			int32(FVoxelLandProgram::ValidateMetered(Invalid, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("non-ASCII preserves output"), Program.Get() == Prior);
		Invalid = Draft;
		Invalid.Parameters[0].Name = FString::ChrN(257, TCHAR('a'));
		TestEqual(TEXT("overlong name"),
			int32(FVoxelLandProgram::ValidateMetered(Invalid, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("overlong preserves output"), Program.Get() == Prior);
		Invalid = Draft;
		Invalid.Seeds[1] = Invalid.Seeds[0];
		TestEqual(TEXT("duplicate seed"),
			int32(FVoxelLandProgram::ValidateMetered(Invalid, Budget.ToSharedRef(), NeverCancel, Program).Code),
			int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("duplicate seed preserves output"), Program.Get() == Prior);
		// This fixture has 3 parameter preflight checks, 11 copy checks, three
		// constant and three parameter checks, then the second seed's first
		// comparison. Poll 26 is inside that inner comparison loop.
		int32 SeedPolls = 0;
		auto CancelSeedComparison = [&SeedPolls] {
			return ++SeedPolls == 26;
		};
		TestEqual(TEXT("inner seed comparison cancels"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), CancelSeedComparison, Program).Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestEqual(TEXT("reached inner seed poll"), SeedPolls, 26);
		TestTrue(TEXT("inner cancellation preserves output"), Program.Get() == Prior);
		// The last successful poll is the publication guard. Cancel one poll
		// earlier to exercise the post-hash guard while retaining the prior result.
		int32 HashPolls = 0;
		auto CancelAfterHash = [&HashPolls, SuccessfulPolls] {
			return ++HashPolls == SuccessfulPolls - 1;
		};
		TestEqual(TEXT("post-hash cancellation"),
			int32(FVoxelLandProgram::ValidateMetered(Draft, Budget.ToSharedRef(), CancelAfterHash, Program).Code),
			int32(EVoxelLandM5ErrorCode::Cancelled));
		TestEqual(TEXT("post-hash poll reached"), HashPolls, SuccessfulPolls - 1);
		TestTrue(TEXT("post-hash cancellation preserves output"), Program.Get() == Prior);
		return true;
	}).Get();
}

#endif
