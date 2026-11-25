#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4AirContinuation.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5DefinitionValidator.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FVoxelLandM4AirContinuation>);
static_assert(!std::is_copy_assignable_v<FVoxelLandM4AirContinuation>);
namespace
{
bool MakeAirContinuationControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
{
	FVoxelLandGraphNodeRecord Constant, Output;
	Constant.Id = FGuid(1, 1, 1, 1); Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2); Constant.Pins[0].Id = FGuid(2, 1, 1, 1); Constant.Pins[1].Id = FGuid(2, 1, 1, 2); Constant.Pins[1].bOutput = true;
	Output.Id = FGuid(1, 1, 1, 2); Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1); Output.Pins[0].Id = FGuid(2, 1, 1, 3);
	FVoxelLandGraphLinkRecord Link; Link.SourceNode = Constant.Id; Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id; Link.TargetPin = Output.Pins[0].Id;
	Out.Source.Nodes = {Constant, Output}; Out.Source.Links = {Link};
	FVoxelLandProgramDraft Draft; TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error) || FVoxelLandProgram::Validate(Draft, Program) || !Program
		|| !FVoxelLandProgramCodec::Pack(Out.Source, *Program, Out.Payload, Error)) return false;
	FVoxelLandM4CurveAuthoringKey First, Last; Last.InputQ16 = 65535;
	Out.CurveKeys = {First, Last}; Out.CookedCurve.SourceKeys = Out.CurveKeys; Out.CookedCurve.BakeVersion = 1;
	Out.CookedCurve.SamplesQ8.Init(0, 4097); FCanonicalWriter Writer;
	for (int32 Sample : Out.CookedCurve.SamplesQ8) Writer.U32(uint32(Sample));
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	FMemory::Memcpy(&Out.CookedCurve.SamplesHash, &Hash, sizeof(Hash)); return true;
}
void ApplyAirContinuationControl(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Owner);
	Graph->SourceVersion = Source.Source.SourceVersion; Graph->Parameters = Source.Source.Parameters;
	Graph->Nodes = Source.Source.Nodes; Graph->Links = Source.Source.Links; Graph->CompiledProgram = Source.Payload;
	Target.Graph = Graph; Target.CurveKeys = Source.CurveKeys; Target.CookedCurve = Source.CookedCurve;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4AirContinuationRealTest, "VoxelLand.M4.AirContinuation.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4AirContinuationRealTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread())) return false;
	FVoxelLandM4ControlSnapshot Control; FString Error; bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeAirContinuationControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl)) return false;
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	ApplyAirContinuationControl(*Config.Get(), Config->Continental, Control);
	ApplyAirContinuationControl(*Config.Get(), Config->Mountain, Control);
	ApplyAirContinuationControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental; Asset->Common.Mountain = Config->Mountain; Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345; Asset->Common.BaseHeightQ8 = 1050 * 256;
	Asset->Common.Caves.bEnabled = true; Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.Limits.MaxValidationWork = 100000000;
	Asset->Finite.bGlobalErosion = false; Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800; Asset->Finite.GridSpacingCm = 6400;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Air continuation fixture requires the M4 process budget to be 256 MiB")); return false;
	}
	const auto Never = [] { return false; };
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result; if (Generator.CreateWorker(Worker)) Result.Code = EVoxelLandM5ErrorCode::InvalidInput; return Result;
	};
	bool Cases[6] = {}; bool bBoundary = false, bHigh = false, bBelowPlanningTop = false;
	bool bForeign = false, bForeignDefinition = false, bOwners = false, bOutside = false, bPlanningInfluence = false;
	bool bWrongWorker = false, bForeignWorker = false, bCancel = false, bExhausted = false;
	FString Failure;
	for (int32 Mode = 0; Mode < 6; ++Mode)
	{
		Asset->Common.BaseHeightQ8 = (Mode == 5 ? 5050 : 1050) * 256;
		Asset->Common.bNormalizeSlope = Mode == 1;
		Asset->Common.HeightDetail.bEnabled = Mode == 2; Asset->Common.HeightDetail.AmplitudeQ8 = 300 * 256;
		Asset->Common.bOverhangEnabled = Mode == 3; Asset->Common.OverhangAmplitudeQ8 = 500 * 256;
		Asset->Common.OverhangBandQ8 = 5000 * 256; Asset->Common.OverhangFrequencyPerCm = 0.004f;
		Asset->Common.OverhangNodeId = FGuid(11, 22, 33, 44);
		Asset->Common.bSmoothEnabled = Mode == 4; Asset->Common.SmoothStrength = 1.f;
		Asset->Finite.MinZCm = -12800; Asset->Finite.MaxZCm = Mode == 5 ? 4001 : 12801;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Source, Job, Foreign, Tiny;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Source)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Job)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Foreign)
			|| !FVoxelLandM5Budget::TryCreate(1, 1, Tiny)) return false;
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Source.ToSharedRef(), Capture);
		if (Captured.Code != EVoxelLandM5ErrorCode::None || !Capture) { Failure = Captured.Message; continue; }
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> ForeignCapture;
		if (Mode == 0)
		{
			const auto ForeignCaptured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Foreign.ToSharedRef(), ForeignCapture);
			if (ForeignCaptured.Code != EVoxelLandM5ErrorCode::None || !ForeignCapture) { Failure = ForeignCaptured.Message; continue; }
		}
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
			auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
			FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
			Gate.MinInclusive.X = Gate.MinInclusive.Y = -4000; Gate.MaxExclusive.X = Gate.MaxExclusive.Y = 4000;
			Gate.MinInclusive.Z = -4000; Gate.MaxExclusive.Z = Mode == 5 ? -3200 : 4000;
			FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, Source.ToSharedRef(), Never, Definition);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition) { Failure = Result.Message; return; }
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
			Result = FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Source.ToSharedRef(), Never, Influence);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence) { Failure = Result.Message; return; }
			if (Mode == 5) bPlanningInfluence = true;
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
			Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(Capture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Baseline) { Failure = Result.Message; return; }
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) return;
			TSharedPtr<const FVoxelLandM4AirContinuation, ESPMode::ThreadSafe> Air;
			Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), Never, Air);
			Failure = Result.Message;
			// Native detail and overhang provider validity over the complete
			// numeric domain is not established; the primitive must fail closed.
			if (Mode == 2 || Mode == 3)
			{
				Cases[Mode] = Result.Code == EVoxelLandM5ErrorCode::Unproven && !Air;
				return;
			}
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Air) return;
			const int64 First = Air->GetFirstLatticeZ(), Last = Air->GetLastLatticeZ();
			FVoxelLandM5PointCm Probe; Probe.X = -1; Probe.Y = -1; Probe.Z = First - 1;
			TArray<int16> Values;
			auto Sample = Baseline->GetGenerator().GenerateFiniteM4Box(Probe, FIntVector(1, 1, 2), *Worker, Job.ToSharedRef(), Never, Values);
			Cases[Mode] = Sample.Code == EVoxelLandM5ErrorCode::None && Values.Num() == 2 && Values[1] > 0 && First <= Last;
			if (Mode == 0) bBoundary = Cases[Mode] && First == 11 && Values[0] <= 0;
			if (Mode == 5) bBelowPlanningTop = Cases[Mode] && First * 100 > 4001;
			Probe.Z = Last; Values.Reset();
			Sample = Baseline->GetGenerator().GenerateFiniteM4Box(Probe, FIntVector(1, 1, 1), *Worker, Job.ToSharedRef(), Never, Values);
			Cases[Mode] &= Sample.Code == EVoxelLandM5ErrorCode::None && Values.Num() == 1 && Values[0] > 0
				&& Last == (Mode == 4 ? 19999 : 20000);
			if (Mode == 0) bHigh = Cases[Mode];
			if (Mode != 0) return;
			const auto Prior = Air;
			bOwners = Air->Matches(*Baseline, *Influence, *Source, *Job) && !Air->Matches(*Baseline, *Influence, *Source, *Foreign)
				&& Air->GetMinXY() == FIntPoint(-2, -2) && Air->GetMaxXYExclusive() == FIntPoint(1, 1);
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> OtherDefinition;
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> OtherInfluence;
			if (FVoxelLandM5DefinitionValidator::Validate(Inputs, Source.ToSharedRef(), Never, OtherDefinition).Code == EVoxelLandM5ErrorCode::None
				&& OtherDefinition && FVoxelLandM5InfluenceSet::BuildFinite(OtherDefinition.ToSharedRef(), Source.ToSharedRef(), Never, OtherInfluence).Code == EVoxelLandM5ErrorCode::None && OtherInfluence)
			{
				Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), OtherInfluence.ToSharedRef(), Source.ToSharedRef(),
					*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), Never, Air);
				bForeignDefinition = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Air == Prior;
			}
			Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, FIntPoint(200, 0), FIntPoint(201, 1), Job.ToSharedRef(), Never, Air);
			bOutside = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Air == Prior;
			auto ForeignSnapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(ForeignCapture->GetSnapshot());
			FVoxelLandM5DefinitionInputs ForeignInputs{ForeignSnapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> ForeignDefinition;
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> OtherBaseline;
			if (FVoxelLandM5DefinitionValidator::Validate(ForeignInputs, Foreign.ToSharedRef(), Never, ForeignDefinition).Code == EVoxelLandM5ErrorCode::None
				&& ForeignDefinition && FVoxelLandM5CapturedFiniteScratch::BuildBound(ForeignCapture.ToSharedRef(), ForeignDefinition.ToSharedRef(), Factory, Never, OtherBaseline).Code == EVoxelLandM5ErrorCode::None && OtherBaseline)
			{
				TUniquePtr<FVoxelLandWorkerContext> OtherWorker;
				if (!OtherBaseline->GetGenerator().CreateWorker(OtherWorker) && OtherWorker)
				{
					Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
						*OtherWorker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), Never, Air);
					bForeignWorker = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Air == Prior;
				}
			}
			Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Foreign.ToSharedRef(),
				*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), Never, Air);
			bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Air == Prior;
			Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Tiny.ToSharedRef(), Never, Air);
			bExhausted = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Air == Prior;
			Result = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), [] { return true; }, Air);
			bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Air == Prior;
			Async(EAsyncExecution::ThreadPool, [&] {
				auto Other = Air;
				const auto Wrong = FVoxelLandM4AirContinuation::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
					*Worker, FIntPoint(-2, -2), FIntPoint(1, 1), Job.ToSharedRef(), Never, Other);
				bWrongWorker = Wrong.Code == EVoxelLandM5ErrorCode::InvalidInput && Other == Prior;
			}).Wait();
		}).Wait();
	}
	bool bAll = true;
	for (int32 Mode = 0; Mode < 6; ++Mode)
	{
		TestTrue(FString::Printf(TEXT("real source continuation mode %d: %s"), Mode, *Failure), Cases[Mode]);
		bAll &= Cases[Mode];
	}
	TestTrue(TEXT("flat threshold minus one and threshold independently sampled"), bBoundary);
	TestTrue(TEXT("numeric last lattice sample is positive"), bHigh);
	TestTrue(TEXT("below-terrain planning gate has valid padded coverage"), bPlanningInfluence);
	TestTrue(TEXT("planning top below terrain does not imply air"), bBelowPlanningTop);
	TestTrue(TEXT("continuation retains exact owners and negative XY extent"), bOwners);
	TestTrue(TEXT("foreign equal definition preserves output"), bForeignDefinition);
	TestTrue(TEXT("outside finite XY source preserves output"), bOutside);
	TestTrue(TEXT("foreign source budget preserves output"), bForeign);
	TestTrue(TEXT("foreign generator worker preserves output"), bForeignWorker);
	TestTrue(TEXT("wrong-thread worker preserves output"), bWrongWorker);
	TestTrue(TEXT("budget exhaustion preserves output"), bExhausted);
	TestTrue(TEXT("cancellation preserves output"), bCancel);
	return bAll && bBoundary && bHigh && bPlanningInfluence && bBelowPlanningTop && bOwners && bForeignDefinition && bOutside && bForeign && bForeignWorker && bWrongWorker && bExhausted && bCancel;
}
#endif
