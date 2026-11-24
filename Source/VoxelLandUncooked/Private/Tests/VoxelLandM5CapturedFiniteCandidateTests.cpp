#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandM5ProtectionAsset.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedBaseline.h"
#include "World/VoxelLandM5CapturedFiniteCandidate.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5FiniteBaseScratch.h"
#include "World/VoxelLandM5MeteredIdentity.h"

class FVoxelLandM5CapturedCandidateFixture final
{
public:
	static bool MakeControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
	{
		FVoxelLandGraphNodeRecord Constant, Output;
		Constant.Id = FGuid(1, 1, 1, 1);
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Constant.Pins.SetNum(2);
		Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
		Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
		Constant.Pins[1].bOutput = true;
		Output.Id = FGuid(1, 1, 1, 2);
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = FGuid(2, 1, 1, 3);
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Constant.Id;
		Link.SourcePin = Constant.Pins[1].Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
		Out.Source.Nodes = {Constant, Output};
		Out.Source.Links = {Link};
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error)
			|| FVoxelLandProgram::Validate(Draft, Program) || !Program
			|| !FVoxelLandProgramCodec::Pack(Out.Source, *Program, Out.Payload, Error))
		{
			return false;
		}
		FVoxelLandM4CurveAuthoringKey First, Last;
		Last.InputQ16 = 65535;
		Out.CurveKeys = {First, Last};
		Out.CookedCurve.SourceKeys = Out.CurveKeys;
		Out.CookedCurve.BakeVersion = 1;
		Out.CookedCurve.SamplesQ8.Init(0, 4097);
		FCanonicalWriter Writer;
		for (int32 Sample : Out.CookedCurve.SamplesQ8)
		{
			Writer.U32(uint32(Sample));
		}
		const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
		FMemory::Memcpy(&Out.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		return true;
	}
	static void Apply(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target,
		const FVoxelLandM4ControlSnapshot& Source)
	{
		UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Owner);
		Graph->SourceVersion = Source.Source.SourceVersion;
		Graph->Parameters = Source.Source.Parameters;
		Graph->Nodes = Source.Source.Nodes;
		Graph->Links = Source.Source.Links;
		Graph->CompiledProgram = Source.Payload;
		Target.Graph = Graph;
		Target.CurveKeys = Source.CurveKeys;
		Target.CookedCurve = Source.CookedCurve;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CapturedFiniteCandidateTest, "VoxelLand.M5.CapturedFiniteCandidate.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5CapturedFiniteCandidateTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture fixture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = FVoxelLandM5CapturedCandidateFixture::MakeControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	FVoxelLandM5CapturedCandidateFixture::Apply(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5CapturedCandidateFixture::Apply(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5CapturedCandidateFixture::Apply(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(NewObject<UVoxelLandFiniteWorld>());
	Definition->Common.Continental = Config->Continental;
	Definition->Common.Mountain = Config->Mountain;
	Definition->Common.Peaks = Config->Peaks;
	Definition->Common.Caves.bEnabled = true;
	Definition->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Definition->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Definition->Common.Caves.Topology.MinChambers = 1;
	Definition->Common.Caves.Topology.MaxChambers = 1;
	Definition->Common.Caves.Topology.MinLoops = 0;
	Definition->Common.Caves.Topology.MaxLoops = 0;
	Definition->Common.Caves.Topology.EntranceCount = 0;
	Definition->Common.Caves.Limits.MaxValidationWork = 100000000;
	Definition->Finite.bGlobalErosion = false;
	Definition->Finite.CellsX = Definition->Finite.CellsY = 4;
	Definition->Finite.OriginXCm = Definition->Finite.OriginYCm = -12800;
	Definition->Finite.GridSpacingCm = 6400;
	Definition->Finite.MinZCm = -9000;
	Definition->Finite.MaxZCm = 11000;
	UVoxelLandM5ProtectionAsset* Mask = NewObject<UVoxelLandM5ProtectionAsset>(Definition.Get());
	Mask->CellCounts = FIntVector(3, 3, 3);
	Mask->SpacingCm = 10;
	Mask->Cells.Init(0, 27);
	Mask->Cells[0] = 1;
	Definition->ProtectionMask = Mask;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 budget cvar"), ProcessBudget))
	{
		return false;
	}
	if (ProcessBudget->GetInt() != 256)
	{
		AddWarning(TEXT("Captured finite fixture skipped: M4 budget is not 256 MiB"));
		return true;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const FVoxelLandM5Diagnostic Captured = FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	if (Captured.Code == EVoxelLandM5ErrorCode::BudgetExceeded
		&& Captured.Message == TEXT("M4 preparation reservation unavailable"))
	{
		AddWarning(TEXT("Captured finite fixture skipped: M4 process admission unavailable"));
		return true;
	}
	if (!TestTrue(FString::Printf(TEXT("typed mask capture: %s"), *Captured.Message),
		Captured.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
	{
		return false;
	}
	const auto Factory = [](const FVoxelLandGenerator& Generator,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	};
	const auto Never = [] { return false; };
	TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
	const FVoxelLandM5Diagnostic WrongThread = FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
	TestTrue(TEXT("wrong thread does not claim"), WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && !Scratch);
	bool bBuilt = false, bIdentity = false, bOneShot = false, bRetained = false;
	bool bBaseline = false, bBaselineAtomic = false, bBaselineCancelled = false;
	bool bCandidate = false, bCandidateAtomic = false, bCandidateCancelled = false;
	TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate;
	TWeakPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> WeakBaseline;
	TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Baseline;
	TWeakPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> WeakScratch;
	TWeakPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> WeakCapture = Capture;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Expected;
		const FVoxelLandM5Diagnostic Identity = FVoxelLandM5MeteredIdentity::Capture(
			Capture->GetSnapshot(), *Capture->GetProtectionGrid(), Budget.ToSharedRef(), Never, Expected);
		const FVoxelLandM5Diagnostic Built = FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
		Failure = Built.Message;
		int32 Height = 0;
		bBuilt = Built.Code == EVoxelLandM5ErrorCode::None && Scratch
			&& &Scratch->GetCapture() == Capture.Get() && Scratch->GetScratch().GetGrid().Sample(0, 0, Height);
		bIdentity = bBuilt && Identity.Code == EVoxelLandM5ErrorCode::None && Expected
			&& Scratch->GetIdentity().GetIdentity().CanonicalBytes == Expected->GetIdentity().CanonicalBytes
			&& Capture->GetSnapshot().Manifest.Descriptor == 3;
		if (bBuilt)
		{
			WeakScratch = Scratch;
			const FVoxelLandM5Diagnostic Window = FVoxelLandM5CapturedBaseline::Build(
				Scratch.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, Baseline);
			bBaseline = Window.Code == EVoxelLandM5ErrorCode::None && Baseline
				&& &Baseline->GetCaptured() == Scratch.Get()
				&& &Baseline->GetWindow().GetGenerator() == &Scratch->GetScratch().GetGenerator()
				&& Baseline->GetWindow().IsOwnedBy(*Budget) && Baseline->GetWindow().GetDensity().Num() == 35 * 35 * 35;
			if (bBaseline)
			{
				WeakBaseline = Baseline;
				const FVoxelLandM5Diagnostic Planned = FVoxelLandM5CapturedFiniteCandidate::Build(
					Baseline.ToSharedRef(), 1, Never, Candidate);
				const FVoxelLandM5CavePlan* Plan = Candidate ? &Candidate->GetCandidate().GetPlan() : nullptr;
				bCandidate = Planned.Code == EVoxelLandM5ErrorCode::None && Candidate && Plan
					&& &Candidate->GetSource() == Baseline.Get()
					&& Candidate->GetCandidate().UsesBudget(*Budget)
					&& Candidate->GetCandidate().MatchesSource(Capture->GetSnapshot().Caves,
						Capture->GetSnapshot().Base.Seed, FVoxelLandM5RegionCoord{})
					&& Plan->Chambers.Num() == 1 && Plan->Entrances.IsEmpty();
				const TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> PriorCandidate = Candidate;
				const FVoxelLandM5Diagnostic InvalidCandidate = FVoxelLandM5CapturedFiniteCandidate::Build(
					Baseline.ToSharedRef(), 0, Never, Candidate);
				bCandidateAtomic = InvalidCandidate.Code == EVoxelLandM5ErrorCode::InvalidInput && Candidate == PriorCandidate;
				const FVoxelLandM5Diagnostic CancelCandidate = FVoxelLandM5CapturedFiniteCandidate::Build(
					Baseline.ToSharedRef(), 1, [] { return true; }, Candidate);
				bCandidateCancelled = CancelCandidate.Code == EVoxelLandM5ErrorCode::Cancelled && Candidate == PriorCandidate;
			}
			const TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Prior = Baseline;
			const FVoxelLandM5Diagnostic Invalid = FVoxelLandM5CapturedBaseline::Build(
				Scratch.ToSharedRef(), FIntVector(1, 1, 1), FIntVector(1, 1, 1), 1, Never, Baseline);
			bBaselineAtomic = Invalid.Code != EVoxelLandM5ErrorCode::None && Baseline == Prior;
			const FVoxelLandM5Diagnostic Cancel = FVoxelLandM5CapturedBaseline::Build(
				Scratch.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1,
				[] { return true; }, Baseline);
			bBaselineCancelled = Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Baseline == Prior;
		}
		const TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Previous = Scratch;
		const FVoxelLandM5Diagnostic Again = FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
		bOneShot = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && Scratch == Previous;
		Capture.Reset();
		bRetained = WeakCapture.IsValid() && Scratch.IsValid();
	}).Wait();
	TestTrue(FString::Printf(TEXT("captured scratch succeeds: %s"), *Failure), bBuilt);
	TestTrue(TEXT("mask identity is exact"), bIdentity);
	TestTrue(TEXT("one-shot claim preserves output"), bOneShot);
	TestTrue(TEXT("result retains capture"), bRetained);
	Scratch.Reset();
	TestTrue(TEXT("baseline retains exact captured scratch"), bBaseline && WeakScratch.IsValid() && WeakCapture.IsValid());
	TestTrue(TEXT("invalid window preserves output"), bBaselineAtomic);
	TestTrue(TEXT("cancelled window preserves output"), bBaselineCancelled);
	Baseline.Reset();
	TestTrue(TEXT("captured zero-entrance candidate uses exact source"), bCandidate && WeakBaseline.IsValid());
	TestTrue(TEXT("invalid candidate cap preserves output"), bCandidateAtomic);
	TestTrue(TEXT("cancelled candidate preserves output"), bCandidateCancelled);
	Candidate.Reset();
	TestFalse(TEXT("final result release releases baseline"), WeakBaseline.IsValid());
	TestFalse(TEXT("final result release releases scratch"), WeakScratch.IsValid());
	TestFalse(TEXT("final result release releases capture"), WeakCapture.IsValid());
	// The captured mask uses 10 cm spacing and the baseline uses 100 cm.
	// One requested entrance must reach QueryFlat, which rejects this exact
	// source-bound mismatch before any surface coordinates are sampled.
	Definition->Common.Caves.Topology.EntranceCount = 1;
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> MismatchCapture;
	const FVoxelLandM5Diagnostic MismatchAdmission =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), MismatchCapture);
	if (!TestTrue(FString::Printf(TEXT("mask mismatch capture: %s"), *MismatchAdmission.Message),
		MismatchAdmission.Code == EVoxelLandM5ErrorCode::None && MismatchCapture.IsValid()))
	{
		return false;
	}
	bool bMaskMismatch = false;
	FString MismatchFailure;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> MismatchScratch;
		FVoxelLandM5Diagnostic Result = FVoxelLandM5CapturedFiniteScratch::Build(
			MismatchCapture.ToSharedRef(), Factory, Never, MismatchScratch);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !MismatchScratch)
		{
			MismatchFailure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> MismatchBaseline;
		Result = FVoxelLandM5CapturedBaseline::Build(MismatchScratch.ToSharedRef(),
			FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, MismatchBaseline);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !MismatchBaseline)
		{
			MismatchFailure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Rejected;
		Result = FVoxelLandM5CapturedFiniteCandidate::Build(MismatchBaseline.ToSharedRef(), 1000000, Never, Rejected);
		MismatchFailure = Result.Message;
		bMaskMismatch = Result.Code == EVoxelLandM5ErrorCode::InvalidInput
			&& Result.Stage == FName(TEXT("M5.WindowSurfaceQuery"))
			&& Result.Message == TEXT("Protection and baseline spacing differ") && !Rejected;
	}).Wait();
	TestTrue(FString::Printf(TEXT("captured mask spacing reaches surface query: %s"), *MismatchFailure), bMaskMismatch);
	MismatchCapture.Reset();
	Definition->Common.Caves.Topology.EntranceCount = 0;
	Definition->ProtectionMask = nullptr;
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> CancelCapture;
	const FVoxelLandM5Diagnostic SecondCapture =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), CancelCapture);
	if (!TestTrue(FString::Printf(TEXT("no-mask capture: %s"), *SecondCapture.Message),
		SecondCapture.Code == EVoxelLandM5ErrorCode::None && CancelCapture.IsValid()))
	{
		return false;
	}
	bool bCancelled = false, bCancelConsumed = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		const FVoxelLandM5Diagnostic Cancel = FVoxelLandM5CapturedFiniteScratch::Build(
			CancelCapture.ToSharedRef(), Factory, [] { return true; }, Scratch);
		bCancelled = Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && !Scratch;
		const FVoxelLandM5Diagnostic Again = FVoxelLandM5CapturedFiniteScratch::Build(
			CancelCapture.ToSharedRef(), Factory, Never, Scratch);
		bCancelConsumed = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && !Scratch;
	}).Wait();
	TestTrue(TEXT("cancellation consumes reservation without output"), bCancelled && bCancelConsumed);
	return bBuilt;
}
#endif
