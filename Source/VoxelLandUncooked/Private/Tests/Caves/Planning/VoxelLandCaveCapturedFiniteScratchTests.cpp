#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Caves/Field/VoxelLandCaveProtectionAsset.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"
#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Operators/VoxelLandOperator.h"
#include "World/Caves/Validation/VoxelLandCaveDefinitionValidator.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"
#include "World/Caves/Persistence/VoxelLandCaveMeteredIdentity.h"

class FVoxelLandM5CapturedScratchFixture final
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
		if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error) || FVoxelLandProgram::Validate(Draft, Program)
			|| !Program || !FVoxelLandProgramCodec::Pack(Out.Source, *Program, Out.Payload, Error))
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
	static void Apply(
		UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CapturedFiniteScratchTest, "VoxelLand.M5.CapturedFiniteScratch.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5CapturedFiniteScratchTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture fixture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		bControl = FVoxelLandM5CapturedScratchFixture::MakeControl(Control, Error);
	}).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	FVoxelLandM5CapturedScratchFixture::Apply(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5CapturedScratchFixture::Apply(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5CapturedScratchFixture::Apply(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(NewObject<UVoxelLandFiniteWorld>());
	Definition->Common.Continental = Config->Continental;
	Definition->Common.Mountain = Config->Mountain;
	Definition->Common.Peaks = Config->Peaks;
	Definition->Common.Caves.bEnabled = true;
	Definition->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Definition->Common.Caves.StageId = FGuid(11, 22, 33, 44);
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
		AddError(TEXT("Captured finite fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const FVoxelLandM5Diagnostic Captured =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	if (Captured.Code == EVoxelLandM5ErrorCode::BudgetExceeded
		&& Captured.Message == TEXT("M4 preparation reservation unavailable"))
	{
		AddError(TEXT("Captured finite fixture requires an available M4 process admission"));
		return false;
	}
	if (!TestTrue(FString::Printf(TEXT("typed mask capture: %s"), *Captured.Message),
			Captured.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
	{
		return false;
	}
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
							 TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	};
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
	const FVoxelLandM5Diagnostic WrongThread =
		FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
	TestTrue(TEXT("wrong thread does not claim"), WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && !Scratch);
	bool bBuilt = false, bIdentity = false, bOneShot = false, bRetained = false;
	TWeakPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> WeakCapture = Capture;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Expected;
		const FVoxelLandM5Diagnostic Identity = FVoxelLandM5MeteredIdentity::Capture(
			Capture->GetSnapshot(), *Capture->GetProtectionGrid(), Budget.ToSharedRef(), Never, Expected);
		const FVoxelLandM5Diagnostic Built =
			FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
		Failure = Built.Message;
		int32 Height = 0;
		bBuilt = Built.Code == EVoxelLandM5ErrorCode::None && Scratch && &Scratch->GetCapture() == Capture.Get()
			  && Scratch->GetScratch().GetGrid().Sample(0, 0, Height);
		bIdentity = bBuilt && Identity.Code == EVoxelLandM5ErrorCode::None && Expected
				 && Scratch->GetIdentity().GetIdentity().CanonicalBytes == Expected->GetIdentity().CanonicalBytes
				 && Capture->GetSnapshot().Manifest.Descriptor == 3;
		const TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Previous = Scratch;
		const FVoxelLandM5Diagnostic Again =
			FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
		bOneShot = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && Scratch == Previous;
		Capture.Reset();
		bRetained = WeakCapture.IsValid() && Scratch.IsValid();
	}).Wait();
	TestTrue(FString::Printf(TEXT("captured scratch succeeds: %s"), *Failure), bBuilt);
	TestTrue(TEXT("mask identity is exact"), bIdentity);
	TestTrue(TEXT("one-shot claim preserves output"), bOneShot);
	TestTrue(TEXT("result retains capture"), bRetained);
	Scratch.Reset();
	TestFalse(TEXT("final result release releases capture"), WeakCapture.IsValid());
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
		const FVoxelLandM5Diagnostic Again =
			FVoxelLandM5CapturedFiniteScratch::Build(CancelCapture.ToSharedRef(), Factory, Never, Scratch);
		bCancelConsumed = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && !Scratch;
	}).Wait();
	TestTrue(TEXT("cancellation consumes reservation without output"), bCancelled && bCancelConsumed);
	CancelCapture.Reset();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BoundBudget, ForeignBudget;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, BoundBudget)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, ForeignBudget))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> BoundCapture;
	const auto BoundAdmission =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), BoundBudget.ToSharedRef(), BoundCapture);
	if (!TestTrue(FString::Printf(TEXT("bound capture admission: %s"), *BoundAdmission.Message),
			BoundAdmission.Code == EVoxelLandM5ErrorCode::None && BoundCapture))
	{
		return false;
	}
	bool bBound = false, bForeignBudget = false, bForeignIdentity = false, bBoundCancel = false, bBoundOneShot = false;
	FString BoundFailure;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(BoundCapture->GetSnapshot());
		auto Registry =
			MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Snapshot->Caves.Bounds, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Valid, Foreign, Different;
		const auto ValidResult =
			FVoxelLandM5DefinitionValidator::Validate(Inputs, BoundBudget.ToSharedRef(), Never, Valid);
		if (ValidResult.Code != EVoxelLandM5ErrorCode::None || !Valid)
		{
			BoundFailure = ValidResult.Message;
			return;
		}
		const auto ForeignResult =
			FVoxelLandM5DefinitionValidator::Validate(Inputs, ForeignBudget.ToSharedRef(), Never, Foreign);
		Snapshot->Base.Seed ^= 1;
		const auto DifferentResult =
			FVoxelLandM5DefinitionValidator::Validate(Inputs, BoundBudget.ToSharedRef(), Never, Different);
		Snapshot->Base.Seed ^= 1;
		if (ForeignResult.Code != EVoxelLandM5ErrorCode::None || DifferentResult.Code != EVoxelLandM5ErrorCode::None
			|| !Foreign || !Different)
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
		const auto ForeignCheck = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			BoundCapture.ToSharedRef(), Foreign.ToSharedRef(), Factory, Never, Baseline);
		bForeignBudget = ForeignCheck.Code == EVoxelLandM5ErrorCode::InvalidInput && !Baseline;
		const auto DifferentCheck = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			BoundCapture.ToSharedRef(), Different.ToSharedRef(), Factory, Never, Baseline);
		bForeignIdentity = DifferentCheck.Code == EVoxelLandM5ErrorCode::InvalidInput && !Baseline;
		const auto CancelCheck = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			BoundCapture.ToSharedRef(), Valid.ToSharedRef(), Factory, [] { return true; }, Baseline);
		bBoundCancel = CancelCheck.Code == EVoxelLandM5ErrorCode::Cancelled && !Baseline;
		const auto BuiltBound = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			BoundCapture.ToSharedRef(), Valid.ToSharedRef(), Factory, Never, Baseline);
		BoundFailure = BuiltBound.Message;
		int32 Height = 0;
		bBound = BuiltBound.Code == EVoxelLandM5ErrorCode::None && Baseline && &Baseline->GetDefinition() == Valid.Get()
			  && Baseline->UsesBudget(*BoundBudget) && Baseline->GetGrid().Sample(0, 0, Height)
			  && Baseline->GetGenerator().GetM4HeightGrid().Get() == &Baseline->GetGrid();
		const auto Prior = Baseline;
		const auto Again = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			BoundCapture.ToSharedRef(), Valid.ToSharedRef(), Factory, Never, Baseline);
		bBoundOneShot = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && Baseline == Prior;
	}).Wait();
	TestTrue(FString::Printf(TEXT("bound baseline: %s"), *BoundFailure), bBound);
	TestTrue(TEXT("foreign budget preserves output and capture"), bForeignBudget);
	TestTrue(TEXT("foreign descriptor preserves output and capture"), bForeignIdentity);
	TestTrue(TEXT("bound cancellation preserves output and capture"), bBoundCancel);
	TestTrue(TEXT("bound capture consumed once"), bBoundOneShot);
	BoundCapture.Reset();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> WorkerBudget;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, WorkerBudget))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> WorkerCapture;
	const auto WorkerAdmission =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), WorkerBudget.ToSharedRef(), WorkerCapture);
	if (!TestTrue(TEXT("foreign worker capture admission"),
			WorkerAdmission.Code == EVoxelLandM5ErrorCode::None && WorkerCapture))
	{
		return false;
	}
	bool bWrongWorker = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(WorkerCapture->GetSnapshot());
		auto Registry =
			MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Snapshot->Caves.Bounds, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Valid;
		if (FVoxelLandM5DefinitionValidator::Validate(Inputs, WorkerBudget.ToSharedRef(), Never, Valid).Code
				!= EVoxelLandM5ErrorCode::None
			|| !Valid)
		{
			return;
		}
		int32 ForeignWorkers = 0;
		const auto WrongFactory = [&ForeignWorkers](const FVoxelLandGenerator& Expected,
									  TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
									  TUniquePtr<FVoxelLandWorkerContext>& Worker) {
			FVoxelLandM5Diagnostic Result;
			FVoxelLandProgramDraft Draft;
			Draft.RegisterCount = 1;
			FVoxelLandInstruction Input, Store;
			Input.Op = EVoxelLandOpcode::LoadZ;
			Store.Op = EVoxelLandOpcode::StoreDensity;
			Draft.Instructions = {Input, Store};
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			FVoxelLandGeneratorSettings Settings;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Other;
			if (FVoxelLandProgram::Validate(Draft, Program) || !Program)
			{
				Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
				return Result;
			}
			Settings.Program = Program;
			if (FVoxelLandGenerator::Create(Settings, Other) || !Other || Other->CreateWorker(Worker) || !Worker)
			{
				Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
			}
			else if (!Expected.OwnsWorkerOnCurrentThread(*Worker))
			{
				++ForeignWorkers;
			}
			return Result;
		};
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Output;
		const auto Wrong = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			WorkerCapture.ToSharedRef(), Valid.ToSharedRef(), WrongFactory, Never, Output);
		const auto Again = FVoxelLandM5CapturedFiniteScratch::BuildBound(
			WorkerCapture.ToSharedRef(), Valid.ToSharedRef(), Factory, Never, Output);
		bWrongWorker = ForeignWorkers == 3 && Wrong.Code == EVoxelLandM5ErrorCode::Unproven
					&& Wrong.Stage == TEXT("M5.MeteredControlPipeline")
					&& Wrong.Message == TEXT("Worker belongs to a different generator or thread") && !Output
					&& Again.Code == EVoxelLandM5ErrorCode::InvalidInput && !Output;
	}).Wait();
	TestTrue(TEXT("foreign control worker cannot produce a bound baseline"), bWrongWorker);

	// Native capture reserves its process and validation permits before copying
	// authoring inputs; its private source remains bound to the definition.
	const auto NativeBounds = WorkerCapture->GetSnapshot().Caves.Bounds;
	WorkerCapture.Reset();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NativeBudget;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, NativeBudget))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Source;
	const auto CapturedNative =
		FVoxelLandM5BoundBase::CaptureFiniteNative(*Definition.Get(), NativeBudget.ToSharedRef(), 1, Source);
	if (!TestTrue(FString::Printf(TEXT("native capture: %s"), *CapturedNative.Message),
			CapturedNative.Code == EVoxelLandM5ErrorCode::None && Source))
	{
		return false;
	}
	auto Registry =
		MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
	TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> NativeDefinition;
	bool bNative = false, bNativeCancel = false, bNativeOneShot = false, bNativeWrongThread = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		const auto Validated = FVoxelLandM5BoundBase::ValidateFiniteNative(
			Source.ToSharedRef(), Registry, NativeBounds, true, true, Never, NativeDefinition);
		if (Validated.Code != EVoxelLandM5ErrorCode::None || !NativeDefinition)
		{
			return;
		}
	}).Wait();
	if (!TestTrue(TEXT("native definition admitted"), NativeDefinition.IsValid()))
	{
		return false;
	}
	// ReserveValidation has four slots. Hold the remaining three so the next
	// native capture must fail before asset capture, preserving its output.
	TArray<TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>> ValidationProbes;
	for (int32 ProbeIndex = 0; ProbeIndex < 3; ++ProbeIndex)
	{
		auto Permit = VoxelLandM4Preparation::ReserveValidation();
		if (!TestTrue(TEXT("validation isolation permit available"), Permit.IsValid()))
		{
			return false;
		}
		ValidationProbes.Add(MoveTemp(Permit));
	}
	bool bExhausted = false;
	{
		const auto PriorSource = Source;
		const auto Exhausted =
			FVoxelLandM5BoundBase::CaptureFiniteNative(*Definition.Get(), NativeBudget.ToSharedRef(), 1, Source);
		bExhausted = Exhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded
				  && Exhausted.Message == TEXT("M4 snapshot validation permit unavailable") && Source == PriorSource;
	}
	TestTrue(TEXT("validation exhaustion preserves source before capture"), bExhausted);
	ValidationProbes.Reset();
	TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> NativeBaseline;
	const auto WrongNativeThread = FVoxelLandM5BoundBase::BuildFinite(
		Source.ToSharedRef(), NativeDefinition.ToSharedRef(), Factory, Never, NativeBaseline);
	bNativeWrongThread = WrongNativeThread.Code == EVoxelLandM5ErrorCode::InvalidInput && !NativeBaseline;
	Async(EAsyncExecution::ThreadPool, [&] {
		const auto Cancelled = FVoxelLandM5BoundBase::BuildFinite(
			Source.ToSharedRef(), NativeDefinition.ToSharedRef(), Factory, [] { return true; }, NativeBaseline);
		bNativeCancel = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && !NativeBaseline;
		const auto Built = FVoxelLandM5BoundBase::BuildFinite(
			Source.ToSharedRef(), NativeDefinition.ToSharedRef(), Factory, Never, NativeBaseline);
		bNative = Built.Code == EVoxelLandM5ErrorCode::None && NativeBaseline
			   && &NativeBaseline->GetDefinition() == NativeDefinition.Get()
			   && NativeBaseline->UsesBudget(*NativeBudget)
			   && NativeBaseline->GetGenerator().GetM4HeightGrid().Get() == &NativeBaseline->GetGrid();
		const auto Prior = NativeBaseline;
		const auto Again = FVoxelLandM5BoundBase::BuildFinite(
			Source.ToSharedRef(), NativeDefinition.ToSharedRef(), Factory, Never, NativeBaseline);
		bNativeOneShot = Again.Code == EVoxelLandM5ErrorCode::InvalidInput && NativeBaseline == Prior;
	}).Wait();
	TestTrue(TEXT("wrong thread does not claim native source"), bNativeWrongThread);
	TestTrue(TEXT("preclaim cancellation preserves native source"), bNativeCancel);
	TestTrue(TEXT("native baseline binds admitted definition and actual grid"), bNative);
	TestTrue(TEXT("native process admission is one-shot"), bNativeOneShot);
	TWeakPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> WeakSource = Source;
	TWeakPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> WeakDefinition = NativeDefinition;
	Source.Reset();
	TestTrue(TEXT("baseline retains source and definition"),
		WeakSource.IsValid() && WeakDefinition.IsValid() && NativeBaseline.IsValid());
	NativeBaseline.Reset();
	// Definition alone must retain the source's validation permit.
	for (int32 ProbeIndex = 0; ProbeIndex < 3; ++ProbeIndex)
	{
		auto Permit = VoxelLandM4Preparation::ReserveValidation();
		if (!TestTrue(TEXT("definition lifetime isolation permit available"), Permit.IsValid()))
		{
			return false;
		}
		ValidationProbes.Add(MoveTemp(Permit));
	}
	const bool bHeld = !VoxelLandM4Preparation::ReserveValidation().IsValid() && WeakSource.IsValid();
	NativeDefinition.Reset();
	const bool bReleased =
		VoxelLandM4Preparation::ReserveValidation().IsValid() && !WeakSource.IsValid() && !WeakDefinition.IsValid();
	ValidationProbes.Reset();
	TestTrue(TEXT("definition alone retains source validation permit"), bHeld);
	TestTrue(TEXT("final definition release returns validation permit"), bReleased);
	return bBuilt && bBound && bForeignBudget && bForeignIdentity && bBoundCancel && bBoundOneShot && bWrongWorker
		&& bNative && bNativeWrongThread && bNativeCancel && bNativeOneShot && bExhausted && bHeld && bReleased;
}
#endif
