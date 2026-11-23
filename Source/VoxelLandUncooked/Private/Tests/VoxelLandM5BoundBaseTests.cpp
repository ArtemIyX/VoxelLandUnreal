#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM5ProtectionAsset.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Enums/VoxelLandM5PlanningMode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ProtectionSamples.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandM4Preparation.h"
#include "World/VoxelLandM5BoundBase.h"

namespace
{
constexpr EAutomationTestFlags M5BoundBaseTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

// Restore the user's process setting on every return, including explicit skips.
class FM5BoundBaseBudgetRestore final
{
public:
	explicit FM5BoundBaseBudgetRestore(IConsoleVariable& InVariable)
		: Variable(InVariable)
		, Previous(InVariable.GetInt())
	{
		Variable.SetWithCurrentPriority(256);
	}
	~FM5BoundBaseBudgetRestore() { Variable.SetWithCurrentPriority(Previous); }

private:
	IConsoleVariable& Variable;
	int32 Previous;
};

// Worker-only graph compilation. No UObject is accessed from this function.
bool M5BoundBaseMakeControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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

void M5BoundBaseApplyControl(
	UVoxelLandM4WorldConfig& Config, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Config);
	Graph->SourceVersion = Source.Source.SourceVersion;
	Graph->Parameters = Source.Source.Parameters;
	Graph->Nodes = Source.Source.Nodes;
	Graph->Links = Source.Source.Links;
	Graph->CompiledProgram = Source.Payload;
	Target.Graph = Graph;
	Target.CurveKeys = Source.CurveKeys;
	Target.CookedCurve = Source.CookedCurve;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BoundBaseTest, "VoxelLand.M5.Runtime.BoundBase", M5BoundBaseTestFlags)
bool FVoxelLandM5BoundBaseTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("fixture setup runs on the game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bFixture = false;
	Async(EAsyncExecution::ThreadPool, [&Control, &Error, &bFixture] {
		bFixture = M5BoundBaseMakeControl(Control, Error);
	}).Wait();
	if (!TestTrue(TEXT("control graph compiled on worker"), bFixture))
	{
		AddError(Error);
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> ConfigOwner(NewObject<UVoxelLandM4WorldConfig>());
	UVoxelLandM4WorldConfig* Config = ConfigOwner.Get();
	M5BoundBaseApplyControl(*Config, Config->Continental, Control);
	M5BoundBaseApplyControl(*Config, Config->Mountain, Control);
	M5BoundBaseApplyControl(*Config, Config->Peaks, Control);
	Config->bGlobalErosion = false;
	Config->CellsX = Config->CellsY = 4;
	Config->OriginXCm = Config->OriginYCm = -12800;
	Config->GridSpacingCm = 6400;
	Config->Caves.bEnabled = true;
	Config->Caves.StageId = FGuid(11, 22, 33, 44);
	Config->Caves.Bounds.MinInclusive.X = Config->Caves.Bounds.MinInclusive.Y = Config->Caves.Bounds.MinInclusive.Z =
		-10000;
	Config->Caves.Bounds.MaxExclusive.X = Config->Caves.Bounds.MaxExclusive.Y = Config->Caves.Bounds.MaxExclusive.Z =
		10000;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process admission cvar exists"), ProcessBudget))
	{
		return false;
	}
	FM5BoundBaseBudgetRestore Restore(*ProcessBudget);
	if (ProcessBudget->GetInt() != 256)
	{
		AddWarning(TEXT("BoundBase admission fixtures skipped: unable to isolate the M4 budget at 256 MiB"));
		return true;
	}
	// Detect pre-existing active reservations. Reserve() may evict idle M4
	// cache entries; a live reader/producer still prevents the second probe.
	auto ProbeA = VoxelLandM4Preparation::Reserve();
	auto ProbeB = VoxelLandM4Preparation::Reserve();
	if (!ProbeA || !ProbeB)
	{
		AddWarning(TEXT("BoundBase admission fixtures skipped: M4 admission is unavailable or already in use"));
		return true;
	}
	ProbeA.Reset();
	ProbeB.Reset();
	TArray<TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>> ValidationProbes;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto Permit = VoxelLandM4Preparation::ReserveValidation();
		if (!Permit)
		{
			AddWarning(TEXT("BoundBase admission fixtures skipped: M4 validation permits are already in use"));
			return true;
		}
		ValidationProbes.Add(MoveTemp(Permit));
	}
	ValidationProbes.Reset();

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 10000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const FVoxelLandM5Diagnostic Captured = FVoxelLandM5BaseCapture::Capture(*Config, Budget.ToSharedRef(), Capture);
	if (!TestTrue(TEXT("finite capture admitted"), Captured.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
	{
		AddError(Captured.Message);
		return false;
	}
	// An intentionally synthetic test-only sentinel lets both workers prove
	// non-null output preservation. It does not represent a prepared M5 world.
	TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe> Sentinel;
	bool bSentinelCreated = false;
	Async(EAsyncExecution::ThreadPool, [&Control, &Capture, &Sentinel, &bSentinelCreated] {
		FString LocalError;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		FVoxelLandGeneratorSettings Settings;
		if (FVoxelLandProgramCodec::Load(Control.Source, Control.Payload, Program, LocalError))
		{
			Settings.Program = Program;
			if (!FVoxelLandGenerator::Create(Settings, Generator) && Generator.IsValid())
			{
				FVoxelLandM5IdentityRecord DummyIdentity;
				Sentinel = MakeShareable(
					new FVoxelLandM5BoundBase(Capture.ToSharedRef(), Generator.ToSharedRef(), MoveTemp(DummyIdentity)));
				bSentinelCreated = true;
			}
		}
	}).Wait();
	if (!TestTrue(TEXT("synthetic output sentinel created"), bSentinelCreated && Sentinel.IsValid()))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe> Base = Sentinel;
	const auto WrongThread = FVoxelLandM5BoundBase::Prepare(Capture.ToSharedRef(), [] { return false; }, Base);
	TestTrue(TEXT("game-thread preparation rejected without consuming capture"),
		WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && Base == Sentinel);
	TestEqual(TEXT("captured finite seed"), Capture->GetSnapshot().Base.Seed, Config->Seed);
	Config->Seed++;
	TestFalse(TEXT("live asset edit marks capture stale"), Config->MatchesM5Snapshot(Capture->GetSnapshot()));
	TestEqual(TEXT("frozen seed remains unchanged"), Capture->GetSnapshot().Base.Seed, Config->Seed - 1);
	Config->Seed--;
	// Synchronize two distinct native threads before they race the same ticket.
	// Each owns its own output; no UObject is captured by the workers.
	TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> FiniteReservation = Capture->GridLease;
	FEvent* ReadyA = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReadyB = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Proceed = FPlatformProcess::GetSynchEventFromPool(true);
	if (!ReadyA || !ReadyB || !Proceed)
	{
		if (ReadyA)
		{
			FPlatformProcess::ReturnSynchEventToPool(ReadyA);
		}
		if (ReadyB)
		{
			FPlatformProcess::ReturnSynchEventToPool(ReadyB);
		}
		if (Proceed)
		{
			FPlatformProcess::ReturnSynchEventToPool(Proceed);
		}
		AddError(TEXT("Could not allocate synchronized race events"));
		return false;
	}
	FVoxelLandM5Diagnostic ResultA, ResultB;
	TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe> OutputA = Sentinel, OutputB = Sentinel;
	const auto Ticket = Capture.ToSharedRef();
	TFuture<void> WorkerA = Async(EAsyncExecution::Thread, [Ticket, ReadyA, Proceed, &ResultA, &OutputA] {
		ReadyA->Trigger();
		Proceed->Wait();
		ResultA = FVoxelLandM5BoundBase::Prepare(Ticket, [] { return false; }, OutputA);
	});
	TFuture<void> WorkerB = Async(EAsyncExecution::Thread, [Ticket, ReadyB, Proceed, &ResultB, &OutputB] {
		ReadyB->Trigger();
		Proceed->Wait();
		ResultB = FVoxelLandM5BoundBase::Prepare(Ticket, [] { return false; }, OutputB);
	});
	const bool bReadyA = ReadyA->Wait(5000);
	const bool bReadyB = ReadyB->Wait(5000);
	Proceed->Trigger();
	WorkerA.Get();
	WorkerB.Get();
	FPlatformProcess::ReturnSynchEventToPool(ReadyA);
	FPlatformProcess::ReturnSynchEventToPool(ReadyB);
	FPlatformProcess::ReturnSynchEventToPool(Proceed);
	TestTrue(TEXT("both competing workers reached the barrier"), bReadyA && bReadyB);
	TestTrue(TEXT("exactly one worker claims the reservation"),
		(ResultA.Code == EVoxelLandM5ErrorCode::Unproven && ResultB.Code == EVoxelLandM5ErrorCode::InvalidInput)
			|| (ResultB.Code == EVoxelLandM5ErrorCode::Unproven
				&& ResultA.Code == EVoxelLandM5ErrorCode::InvalidInput));
	TestTrue(TEXT("both non-null prior outputs are retained"), OutputA == Sentinel && OutputB == Sentinel);
	TestFalse(TEXT("claimed finite reservation is released on failure"), FiniteReservation.IsValid());
	int64 LiveBytes = 0, PeakBytes = 0, WorkUnits = 0;
	Budget->GetUsage(LiveBytes, PeakBytes, WorkUnits);
	TestEqual(TEXT("only the claiming worker charged work"), WorkUnits, int64(1));

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
	FVoxelLandM5Budget::TryCreate(1, 100, Tiny);
	auto OriginalCapture = Capture;
	const auto Failed = FVoxelLandM5BaseCapture::Capture(*Config, Tiny.ToSharedRef(), Capture);
	TestTrue(TEXT("metadata admission fails atomically"),
		Failed.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Capture == OriginalCapture);

	// Preserve the finite capture reader while proving its claimed reservation
	// has been released; snapshots and their metadata leases may remain alive.
	Capture.Reset();
	Config->bUnlimited = true;
	Config->Caves.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	Config->Caves.Bounds.MinInclusive.X = Config->Caves.Bounds.MinInclusive.Y = -204800;
	Config->Caves.Bounds.MaxExclusive.X = Config->Caves.Bounds.MaxExclusive.Y = 204800;
	// Hold one independent M4 reservation. At the default 256 MiB process limit,
	// retaining the local producer's 128 MiB reservation would block Replacement.
	auto OtherWorld = VoxelLandM4Preparation::Reserve();
	if (!OtherWorld)
	{
		AddWarning(TEXT("Local replacement fixture skipped: another M4 producer acquired admission"));
		return true;
	}
	const auto LocalCapture = FVoxelLandM5BaseCapture::Capture(*Config, Budget.ToSharedRef(), Capture);
	if (LocalCapture.Code == EVoxelLandM5ErrorCode::BudgetExceeded && !Capture.IsValid())
	{
		AddWarning(TEXT("Local replacement fixture skipped: M4 admission became unavailable"));
		return true;
	}
	if (!TestTrue(
			TEXT("local capture admitted"), LocalCapture.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
	{
		AddError(LocalCapture.Message);
		return false;
	}
	FVoxelLandM5Diagnostic Prepared;
	TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> LocalReservation = Capture->GridLease;
	Async(EAsyncExecution::ThreadPool, [&Capture, &Base, &Prepared] {
		Prepared = FVoxelLandM5BoundBase::Prepare(Capture.ToSharedRef(), [] { return false; }, Base);
	}).Wait();
	TestTrue(TEXT("local unmetered work also fails closed"),
		Prepared.Code == EVoxelLandM5ErrorCode::Unproven && Base == Sentinel);
	TestFalse(TEXT("local reservation released while capture remains alive"), LocalReservation.IsValid());
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Replacement;
	const auto Replaced = FVoxelLandM5BaseCapture::Capture(*Config, Budget.ToSharedRef(), Replacement);
	if (Replaced.Code == EVoxelLandM5ErrorCode::BudgetExceeded && !LocalReservation.IsValid())
	{
		AddWarning(TEXT("Local replacement fixture skipped after release: another M4 producer acquired admission"));
		return true;
	}
	if (!TestTrue(TEXT("local reservation is released while capture reader remains"),
			Replaced.Code == EVoxelLandM5ErrorCode::None && Replacement.IsValid()))
	{
		AddError(Replaced.Message);
		return false;
	}
	TestTrue(TEXT("frozen local snapshot records regional mode"),
		Capture->GetSnapshot().Caves.PlanningMode == EVoxelLandM5PlanningMode::Regional);
	FVoxelLandM5Diagnostic Cancelled;
	Async(EAsyncExecution::ThreadPool, [&Replacement, &Base, &Cancelled] {
		Cancelled = FVoxelLandM5BoundBase::Prepare(Replacement.ToSharedRef(), [] { return true; }, Base);
	}).Wait();
	TestTrue(TEXT("cancellation preserves the prior output"),
		Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Base == Sentinel);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWork;
	FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 0, NoWork);
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> WorkCapture;
	const auto WorkAdmission = FVoxelLandM5BaseCapture::Capture(*Config, NoWork.ToSharedRef(), WorkCapture);
	if (WorkAdmission.Code == EVoxelLandM5ErrorCode::BudgetExceeded)
	{
		AddWarning(TEXT("Zero-work fixture skipped: M4 admission became unavailable"));
		return true;
	}
	if (!TestTrue(TEXT("zero-work capture admitted"), WorkAdmission.Code == EVoxelLandM5ErrorCode::None))
	{
		AddError(WorkAdmission.Message);
		return false;
	}
	FVoxelLandM5Diagnostic NoWorkResult;
	Async(EAsyncExecution::ThreadPool, [&WorkCapture, &Base, &NoWorkResult] {
		NoWorkResult = FVoxelLandM5BoundBase::Prepare(WorkCapture.ToSharedRef(), [] { return false; }, Base);
	}).Wait();
	TestTrue(TEXT("zero work fails before unmetered preparation"),
		NoWorkResult.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Base == Sentinel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5TypedFiniteBaseCaptureTest, "VoxelLand.M5.Runtime.TypedFiniteBaseCapture", M5BoundBaseTestFlags)
bool FVoxelLandM5TypedFiniteBaseCaptureTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("fixture requires the game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bFixture = false;
	Async(EAsyncExecution::ThreadPool, [&Control, &Error, &bFixture] {
		bFixture = M5BoundBaseMakeControl(Control, Error);
	}).Wait();
	if (!TestTrue(TEXT("control graph compiled on worker"), bFixture))
	{
		AddError(Error);
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	M5BoundBaseApplyControl(*Config.Get(), Config->Continental, Control);
	M5BoundBaseApplyControl(*Config.Get(), Config->Mountain, Control);
	M5BoundBaseApplyControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(NewObject<UVoxelLandFiniteWorld>());
	Definition->Common.Continental = Config->Continental;
	Definition->Common.Mountain = Config->Mountain;
	Definition->Common.Peaks = Config->Peaks;
	Definition->Common.Seed = -12345;
	Definition->Common.Caves.bEnabled = true;
	Definition->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Definition->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Definition->Finite.bGlobalErosion = false;
	Definition->Finite.CellsX = Definition->Finite.CellsY = 4;
	Definition->Finite.OriginXCm = Definition->Finite.OriginYCm = -12800;
	Definition->Finite.GridSpacingCm = 6400;
	Definition->Finite.MinZCm = -9000;
	Definition->Finite.MaxZCm = 11000;

	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process admission cvar exists"), ProcessBudget))
	{
		return false;
	}
	FM5BoundBaseBudgetRestore Restore(*ProcessBudget);
	if (ProcessBudget->GetInt() != 256)
	{
		AddWarning(TEXT("Typed finite capture fixture skipped: unable to isolate the M4 budget at 256 MiB"));
		return true;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 10000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const FVoxelLandM5Diagnostic Captured =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	if (Captured.Code == EVoxelLandM5ErrorCode::BudgetExceeded)
	{
		AddWarning(TEXT("Typed finite capture fixture skipped: producer or validation admission is unavailable"));
		return true;
	}
	if (!TestTrue(
			TEXT("typed finite capture admitted"), Captured.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
	{
		AddError(Captured.Message);
		return false;
	}
	const FVoxelLandM5WorldSnapshot& Snapshot = Capture->GetSnapshot();
	const TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProducerReservation = Capture->GridLease;
	TestTrue(TEXT("typed capture retains a producer reservation"), ProducerReservation.IsValid());
	TestEqual(TEXT("typed seed retained"), Snapshot.Base.Seed, int64(-12345));
	TestTrue(TEXT("finite mode retained"), Snapshot.Base.Domain.Mode == EVoxelLandM4DomainMode::Finite
											   && Snapshot.Caves.PlanningMode == EVoxelLandM5PlanningMode::Finite);
	TestTrue(TEXT("full upstream bounds retained"),
		Snapshot.Caves.Bounds.MinInclusive.X == -12800 && Snapshot.Caves.Bounds.MaxExclusive.X == 12801
			&& Snapshot.Caves.Bounds.MinInclusive.Z == -9000 && Snapshot.Caves.Bounds.MaxExclusive.Z == 11000);
	TestTrue(TEXT("enabled settings and version retained"),
		Snapshot.Caves.bEnabled && Snapshot.Caves.StageId == Definition->Common.Caves.StageId
			&& Snapshot.Caves.Water.Policy == EVoxelLandM5WaterPolicy::DryWorld
			&& Snapshot.FormatVersion == 1 && Snapshot.Manifest.Descriptor == 2 && Snapshot.Manifest.Composition == 1);
	TestTrue(TEXT("control payload retained"),
		Snapshot.Base.Continental.Payload.ProgramFingerprint == Control.Payload.ProgramFingerprint);

	const TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Prior = Capture;
	bool bProtectionCurrent = false;
	const auto NoMaskCurrent = Capture->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
	TestTrue(TEXT("omitted optional protection remains current"),
		NoMaskCurrent.Code == EVoxelLandM5ErrorCode::None && bProtectionCurrent);
	Definition->Common.Caves.Protection.bProtectAll = !Snapshot.Caves.Protection.bProtectAll;
	const auto NoMaskProtectAllChanged = Capture->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
	TestTrue(TEXT("changed protect-all is stale without a mask"),
		NoMaskProtectAllChanged.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
	Definition->Common.Caves.Protection.bProtectAll = Snapshot.Caves.Protection.bProtectAll;
	const auto NoMaskProtectAllRestored = Capture->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
	TestTrue(TEXT("restored protect-all is current without a mask"),
		NoMaskProtectAllRestored.Code == EVoxelLandM5ErrorCode::None && bProtectionCurrent);
	Definition->Common.Caves.Protection.bRequireMask = true;
	const auto RequiredNow = Capture->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
	TestTrue(TEXT("new required mask is stale"),
		RequiredNow.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
	const auto MissingMask = FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	if (MissingMask.Code == EVoxelLandM5ErrorCode::BudgetExceeded)
	{
		AddWarning(TEXT("Protection capture checks skipped: producer or validation admission unavailable"));
	}
	else
	{
		TestTrue(TEXT("missing required mask preserves output"),
			MissingMask.Code == EVoxelLandM5ErrorCode::InvalidInput && Capture == Prior);
		Definition->Common.Caves.Protection.bRequireMask = false;
		UVoxelLandM5ProtectionAsset* Mask = NewObject<UVoxelLandM5ProtectionAsset>(Definition.Get());
		Mask->CellCounts = FIntVector(3, 3, 3);
		Mask->SpacingCm = 10;
		Mask->Cells.Init(0, 27);
		Mask->Cells[0] = 2;
		Definition->ProtectionMask = Mask;
		const auto BadMask = FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
		TestTrue(TEXT("nonbinary mask preserves output"),
			BadMask.Code == EVoxelLandM5ErrorCode::InvalidInput && Capture == Prior);
		Mask->Cells[0] = 1;
		FVoxelLandFiniteWorldSnapshot SourceSnapshot;
		FString MaskError;
		TestTrue(TEXT("source snapshot captures effective mask requirement"),
			Definition->CaptureSnapshot(SourceSnapshot, MaskError) && SourceSnapshot.Common.Caves.Protection.bRequireMask
				&& !Definition->Common.Caves.Protection.bRequireMask);
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> WithMask;
		const auto MaskCapture = FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), WithMask);
		if (MaskCapture.Code == EVoxelLandM5ErrorCode::BudgetExceeded)
		{
			AddWarning(TEXT("Valid protection capture skipped: producer or validation admission unavailable"));
		}
		else if (TestTrue(TEXT("optional mask captured with effective requirement"),
			MaskCapture.Code == EVoxelLandM5ErrorCode::None && WithMask.IsValid()
				&& WithMask->GetProtectionGrid().IsValid() && WithMask->GetSnapshot().Caves.Protection.bRequireMask))
		{
			bProtectionCurrent = false;
			const auto InitiallyCurrent = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("captured protection matches authoring"),
				InitiallyCurrent.Code == EVoxelLandM5ErrorCode::None && bProtectionCurrent);
			Definition->Common.Caves.Protection.bProtectAll = !WithMask->GetSnapshot().Caves.Protection.bProtectAll;
			const auto MaskProtectAllChanged = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("changed protect-all is stale with a mask"),
				MaskProtectAllChanged.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
			Definition->Common.Caves.Protection.bProtectAll = WithMask->GetSnapshot().Caves.Protection.bProtectAll;
			const auto MaskProtectAllRestored = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("restored protect-all is current with a mask"),
				MaskProtectAllRestored.Code == EVoxelLandM5ErrorCode::None && bProtectionCurrent);
			Mask->Cells[0] = 0;
			const auto Edited = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("edited protection is stale"), Edited.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
			Mask->Cells[0] = 1;
			UVoxelLandM5ProtectionAsset* Replacement = NewObject<UVoxelLandM5ProtectionAsset>(Definition.Get());
			Replacement->CellCounts = Mask->CellCounts;
			Replacement->OriginCm = Mask->OriginCm;
			Replacement->SpacingCm = Mask->SpacingCm;
			Replacement->Cells = Mask->Cells;
			Definition->ProtectionMask = Replacement;
			const auto Identical = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("equivalent replacement has the same protection content"),
				Identical.Code == EVoxelLandM5ErrorCode::None && bProtectionCurrent);
			Replacement->OriginCm.X++;
			const auto Moved = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("changed geometry is stale"), Moved.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
			Definition->ProtectionMask = nullptr;
			const auto Removed = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("removed protection is stale"), Removed.Code == EVoxelLandM5ErrorCode::None && !bProtectionCurrent);
			Definition->ProtectionMask = Mask;
			Mask->Cells[0] = 0;
			FVoxelLandM5PointCm SampleOrigin;
			SampleOrigin.X = SampleOrigin.Y = SampleOrigin.Z = 10;
			TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Samples;
			const auto Projected = WithMask->GetProtectionGrid()->Project(
				FIntVector(1, 1, 1), SampleOrigin, Budget.ToSharedRef(), [] { return false; }, Samples);
			TestTrue(TEXT("captured protection remains immutable after asset edit"),
				Projected.Code == EVoxelLandM5ErrorCode::None && Samples.IsValid()
					&& Samples->GetSamples().Num() == 1 && Samples->GetSamples()[0] == 1);
			FVoxelLandM5Diagnostic WrongThread;
			bProtectionCurrent = true;
			Async(EAsyncExecution::ThreadPool, [&WithMask, &Definition, &WrongThread, &bProtectionCurrent] {
				WrongThread = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			}).Wait();
			TestTrue(TEXT("wrong thread preserves result"),
				WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && bProtectionCurrent);
			Mask->Cells[0] = 1;
			int64 MaxBytes = 0, MaxWork = 0, LiveBytes = 0, PeakBytes = 0, Work = 0;
			Budget->GetLimits(MaxBytes, MaxWork);
			Budget->GetUsage(LiveBytes, PeakBytes, Work);
			TestTrue(TEXT("consume remaining work"), Budget->TryChargeWork(MaxWork - Work));
			bProtectionCurrent = false;
			const auto Exhausted = WithMask->CheckFiniteProtectionCurrent(*Definition.Get(), bProtectionCurrent);
			TestTrue(TEXT("exhausted work preserves result"),
				Exhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded && !bProtectionCurrent);
		}
		Definition->ProtectionMask = nullptr;
	}
	Definition->Common.Caves.Protection.bRequireMask = false;
	Definition->Common.Caves.Limits.MaxBytes = 1;
	const FVoxelLandM5Diagnostic Limited =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	TestTrue(TEXT("over-limit request preserves capture"),
		Limited.Code == EVoxelLandM5ErrorCode::InvalidInput && Capture == Prior);
	Definition->Common.Caves.Limits.MaxBytes = 268435456;
	Definition->Common.Caves.bEnabled = false;
	TestTrue(TEXT("live edits leave frozen capture unchanged"),
		Snapshot.Caves.bEnabled && Snapshot.Caves.Limits.MaxBytes == 268435456);

	FVoxelLandM5Diagnostic Cancelled;
	TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe> Base;
	Async(EAsyncExecution::ThreadPool, [&Capture, &Base, &Cancelled] {
		Cancelled = FVoxelLandM5BoundBase::Prepare(Capture.ToSharedRef(), [] { return true; }, Base);
	}).Wait();
	TestTrue(TEXT("cancelled preparation publishes nothing"),
		Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && !Base.IsValid());
	TestFalse(
		TEXT("cancel releases the producer reservation while capture remains live"), ProducerReservation.IsValid());
	FVoxelLandM5Diagnostic Consumed;
	Async(EAsyncExecution::ThreadPool, [&Capture, &Base, &Consumed] {
		Consumed = FVoxelLandM5BoundBase::Prepare(Capture.ToSharedRef(), [] { return false; }, Base);
	}).Wait();
	TestTrue(
		TEXT("typed producer is one-shot"), Consumed.Code == EVoxelLandM5ErrorCode::InvalidInput && !Base.IsValid());
	const FVoxelLandM5Diagnostic Disabled =
		FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
	if (Disabled.Code == EVoxelLandM5ErrorCode::BudgetExceeded)
	{
		AddWarning(TEXT("Disabled capture check skipped: producer or validation admission became unavailable"));
		TestTrue(TEXT("failed disabled admission preserves capture"), Capture == Prior);
	}
	else
	{
		TestTrue(TEXT("disabled capture rejected atomically"),
			Disabled.Code == EVoxelLandM5ErrorCode::InvalidInput && Capture == Prior);
	}
	return true;
}
#endif
