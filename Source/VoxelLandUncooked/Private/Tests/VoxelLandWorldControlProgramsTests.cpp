#if WITH_DEV_AUTOMATION_TESTS
#include "World/VoxelLandWorldControlPrograms.h"

#include "Async/Async.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandWorldSourceSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"

class FVoxelLandWorldControlProgramsFixture final
{
public:
	static bool MakeControl(FVoxelLandM4ControlSnapshot& Control)
	{
		FVoxelLandM4CurveAuthoringKey First;
		FVoxelLandM4CurveAuthoringKey Last;
		Last.InputQ16 = 65535;
		Last.OutputQ8 = 256;
		Control.CurveKeys = {First, Last};
		Control.CookedCurve.SourceKeys = Control.CurveKeys;
		Control.CookedCurve.BakeVersion = 1;
		FVoxelLandM4CurveKey A;
		FVoxelLandM4CurveKey B;
		B.InputQ16 = 65535;
		B.Output = 256;
		const TArray<FVoxelLandM4CurveKey> Keys = {A, B};
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		FString Error;
		if (!VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Control.CookedCurve.SamplesQ8, Curve, Error))
		{
			return false;
		}
		FCanonicalWriter Writer;
		for (const int32 Sample : Control.CookedCurve.SamplesQ8)
		{
			Writer.U32(uint32(Sample));
		}
		const uint64 CurveHash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
		FMemory::Memcpy(&Control.CookedCurve.SamplesHash, &CurveHash, sizeof(CurveHash));
		FVoxelLandSerializedProgram& Payload = Control.Payload;
		Payload.ProgramVersion = int32(FVoxelLandProgramDraft::CurrentFormatVersion);
		Payload.CompilerSemanticVersion = int32(FVoxelLandProgramDraft::CompilerSemanticVersion);
		Payload.RegisterCount = 1;
		const uint64 SourceHash = 0x0c64ed733e702cc9ULL;
		const uint64 ProgramHash = 0xb934c14fd49b91c3ULL;
		FMemory::Memcpy(&Payload.SourceFingerprint, &SourceHash, sizeof(SourceHash));
		FMemory::Memcpy(&Payload.ProgramFingerprint, &ProgramHash, sizeof(ProgramHash));
		FVoxelLandSerializedInstruction Load;
		Load.Opcode = int32(EVoxelLandOpcode::LoadZ);
		Payload.Instructions.Add(Load);
		FVoxelLandSerializedInstruction Store;
		Store.Opcode = int32(EVoxelLandOpcode::StoreDensity);
		Payload.Instructions.Add(Store);
		return true;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldControlProgramsTest, "VoxelLand.WorldModes.ControlPrograms",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldControlProgramsTest::RunTest(const FString& Parameters)
{
	FVoxelLandFiniteWorldSnapshot Finite;
	if (!TestTrue(TEXT("control fixture created"), FVoxelLandWorldControlProgramsFixture::MakeControl(Finite.Common.Continental)))
	{
		return false;
	}
	Finite.Common.Mountain = Finite.Common.Continental;
	Finite.Common.Peaks = Finite.Common.Continental;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(32 * 1024 * 1024, 500000000, Budget))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InputLease;
	if (!Budget->TryReserveBytes(1024 * 1024, InputLease))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> UndersizedLease;
	if (!Budget->TryReserveBytes(1, UndersizedLease))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OtherLease;
	if (!FVoxelLandM5Budget::TryCreate(1024, 1024, OtherBudget)
		|| !OtherBudget->TryReserveBytes(1, OtherLease))
	{
		return false;
	}
	FVoxelLandInfiniteWorldSnapshot Infinite;
	Infinite.Common = Finite.Common;
	const TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> InfiniteSource =
		MakeShared<FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe>(MoveTemp(Infinite));
	FVoxelLandFiniteWorldSnapshot BadFinite = Finite;
	++BadFinite.Common.Peaks.CookedCurve.SamplesQ8[100];
	const TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> BadSource =
		MakeShared<FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe>(MoveTemp(BadFinite));
	const TSharedRef<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> Source =
		MakeShared<FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe>(MoveTemp(Finite));
	return Async(EAsyncExecution::ThreadPool, [this, Source, InfiniteSource, BadSource, Budget, InputLease, UndersizedLease, OtherLease] {
		TSharedPtr<const FVoxelLandWorldControlPrograms, ESPMode::ThreadSafe> Output;
		const auto NeverCancel = [] { return false; };
		FVoxelLandM5Diagnostic Result = FVoxelLandWorldControlPrograms::Load(
			Source, InputLease.ToSharedRef(), Budget.ToSharedRef(), NeverCancel, Output);
		if (!TestEqual(TEXT("all three controls load"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None))
			|| !TestTrue(TEXT("control product exists"), Output.IsValid()))
		{
			AddError(Result.Message);
			return false;
		}
		TestTrue(TEXT("finite product retains finite mode"),
			Output->GetSource().GetWorldKind() == EVoxelLandWorldKind::Finite);
		TSharedPtr<const FVoxelLandWorldControlPrograms, ESPMode::ThreadSafe> InfiniteOutput;
		Result = FVoxelLandWorldControlPrograms::Load(
			InfiniteSource, InputLease.ToSharedRef(), Budget.ToSharedRef(), NeverCancel, InfiniteOutput);
		TestEqual(TEXT("infinite controls load"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
		if (!TestTrue(TEXT("infinite product exists"), InfiniteOutput.IsValid()))
		{
			return false;
		}
		TestTrue(TEXT("infinite product retains infinite mode"),
			InfiniteOutput->GetSource().GetWorldKind() == EVoxelLandWorldKind::Infinite);
		const FVoxelLandWorldControlPrograms* Previous = Output.Get();
		Result = FVoxelLandWorldControlPrograms::Load(
			Source, OtherLease.ToSharedRef(), Budget.ToSharedRef(), NeverCancel, Output);
		TestEqual(TEXT("foreign source lease rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("foreign lease preserves prior product"), Output.Get() == Previous);
		Result = FVoxelLandWorldControlPrograms::Load(
			Source, UndersizedLease.ToSharedRef(), Budget.ToSharedRef(), NeverCancel, Output);
		TestEqual(TEXT("undersized source lease rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestTrue(TEXT("undersized lease preserves prior product"), Output.Get() == Previous);
		Result = FVoxelLandWorldControlPrograms::Load(
			BadSource, InputLease.ToSharedRef(), Budget.ToSharedRef(), NeverCancel, Output);
		TestEqual(TEXT("corrupt peaks curve rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::InvalidInput));
		TestTrue(TEXT("failure identifies peaks"), Result.Message.Contains(TEXT("Peaks control")));
		TestTrue(TEXT("failed replacement preserves prior product"), Output.Get() == Previous);
		Result = FVoxelLandWorldControlPrograms::Load(
			Source, InputLease.ToSharedRef(), Budget.ToSharedRef(), [] { return true; }, Output);
		TestEqual(TEXT("cancelled load rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
		TestTrue(TEXT("cancellation preserves prior product"), Output.Get() == Previous);
		return true;
	}).Get();
}
#endif
