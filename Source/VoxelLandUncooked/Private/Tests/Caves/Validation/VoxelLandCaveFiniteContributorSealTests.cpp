#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidatePlanner.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Generation/Caves/Validation/VoxelLandCaveFiniteContributorSeal.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldInputs.h"
#include "Generation/Caves/Planning/VoxelLandCaveEvaluateChunk.h"
#include "Generation/Caves/Field/VoxelLandCaveInfluenceSet.h"
#include "Generation/Caves/Planning/VoxelLandCaveValidatedDefinition.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"
#include "World/Caves/Validation/VoxelLandCaveDefinitionValidator.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FVoxelLandM5FiniteContributorSeal>);
static_assert(!std::is_copy_assignable_v<FVoxelLandM5FiniteContributorSeal>);
namespace
{
bool MakeSealControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
void ApplySealControl(
	UVoxelLandWorldConfig& Owner, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
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
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteContributorSealTest, "VoxelLand.M5.FiniteContributorSeal.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteContributorSealTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeSealControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	ApplySealControl(*Config.Get(), Config->Continental, Control);
	ApplySealControl(*Config.Get(), Config->Mountain, Control);
	ApplySealControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorldSource> Asset(NewObject<UVoxelLandFiniteWorldSource>());
	Asset->Common.Continental = Config->Continental;
	Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345;
	Asset->Common.BaseHeightQ8 = 1050 * 256;
	Asset->Common.Caves.bEnabled = true;
	Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Topology.MinChambers = Asset->Common.Caves.Topology.MaxChambers = 2;
	Asset->Common.Caves.Topology.MinChamberRadiusCm = Asset->Common.Caves.Topology.MaxChamberRadiusCm = 100;
	Asset->Common.Caves.Topology.MinTunnelRadiusCm = Asset->Common.Caves.Topology.MaxTunnelRadiusCm = 100;
	Asset->Common.Caves.Topology.MinLoops = Asset->Common.Caves.Topology.MaxLoops = 0;
	Asset->Common.Caves.Limits.MaxValidationWork = 100000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Contributor seal fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	const auto Never = [] {
		return false;
	};
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
							 TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	};
	bool bBuilt = false, bRegion = false, bGate = false, bDeterministic = false, bRetained = false;
	bool bForeignBudget = false, bForeignDefinition = false, bWrongThread = false, bCancelled = false, bLimit = false;
	bool bSmoothRejected = false, bTallRejected = false, bSealedBinding = false, bSealedNetwork = false;
	bool bSlopeBounds = false, bSlopeBox = false, bSlopeRawRejected = false, bSlopePlanner = false;
	FString Failure;
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		Asset->Common.Caves.Topology.EntranceCount = Mode == 0 ? 0 : 1;
		Asset->Common.bSmoothEnabled = Mode == 1;
		Asset->Common.SmoothStrength = 1.f;
		Asset->Common.bNormalizeSlope = Mode == 3;
		Asset->Common.Caves.Capsule.MaxSlopeDegrees = Mode == 3 ? 0 : 45;
		Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = Mode == 3 ? -10400 : -12800;
		Asset->Finite.GridSpacingCm = Mode == 3 ? 4800 : 6400;
		Asset->Finite.MinZCm = Mode == 2 ? -300000 : -12800;
		Asset->Finite.MaxZCm = Mode == 2 ? 300001 : 12801;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, ForeignBudget;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Budget)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, ForeignBudget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), Capture);
		if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message),
				Captured.Code == EVoxelLandM5ErrorCode::None && Capture))
		{
			return false;
		}
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
			auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(
				FVoxelLandOperatorRegistry::Snapshot());
			FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
			Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -4000;
			Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 4000;
			if (Mode == 3)
			{
				Gate.MinInclusive.X = Gate.MinInclusive.Y = -6100;
				Gate.MaxExclusive.X = Gate.MaxExclusive.Y = -5700;
			}
			FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, Definition);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition)
			{
				Failure = Result.Message;
				return;
			}
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
			Result =
				FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Budget.ToSharedRef(), Never, Influence);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence)
			{
				Failure = Result.Message;
				return;
			}
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
			Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(
				Capture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Baseline)
			{
				Failure = Result.Message;
				return;
			}
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker)
			{
				return;
			}
			TSharedPtr<const FVoxelLandM5FiniteContributorSeal, ESPMode::ThreadSafe> Seal;
			if (Mode == 3)
			{
				// Independent inverse-halo oracle: the two covered keys [-3,-1)
				// require raw XY [-10400,-2400], while post-filter samples are
				// inset one slope sample to [-10300,-2500].
				const auto& Read = Influence->GetCoverage().ReadBoundsCm;
				bSlopeBounds = Read.MinInclusive.X == -10400 && Read.MinInclusive.Y == -10400
							&& Read.MaxExclusive.X == -2399 && Read.MaxExclusive.Y == -2399;
				FVoxelLandM5PointCm Min;
				Min.X = Min.Y = -103;
				Min.Z = -128;
				TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
				auto Box = FVoxelLandM5BaselineWindow::BuildBox(Baseline->GetGeneratorShared(), *Worker, Min,
					FIntVector(79, 79, 257), Budget.ToSharedRef(), Never, Window);
				bSlopeBox = Box.Code == EVoxelLandM5ErrorCode::None && Window;
				const auto Previous = Window;
				Min.X = Min.Y = -104;
				Box = FVoxelLandM5BaselineWindow::BuildBox(Baseline->GetGeneratorShared(), *Worker, Min,
					FIntVector(81, 81, 257), Budget.ToSharedRef(), Never, Window);
				bSlopeRawRejected = Box.Code != EVoxelLandM5ErrorCode::None && Window == Previous;
				Window.Reset();
				Result = FVoxelLandM5FiniteContributorSeal::Build(
					Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 1, Never, Seal);
				bSlopePlanner =
					Result.Code == EVoxelLandM5ErrorCode::Unproven && Result.Stage == TEXT("M5.EntrancePlanner")
					&& Result.Message == TEXT("A horizontal-only profile cannot ascend to the surface") && !Seal;
				return;
			}
			Result = FVoxelLandM5FiniteContributorSeal::Build(
				Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 100000, Never, Seal);
			Failure = Result.Message;
			if (Mode == 1)
			{
				bSmoothRejected = Result.Code == EVoxelLandM5ErrorCode::Unproven && !Seal;
				return;
			}
			if (Mode == 2)
			{
				bTallRejected = Result.Code == EVoxelLandM5ErrorCode::Unproven && !Seal;
				return;
			}
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Seal)
			{
				return;
			}
			const auto& Candidate = Seal->GetCandidate();
			FVoxelLandM5CaveSettings Expected = Snapshot->Caves;
			Expected.Bounds = Gate;
			const FVoxelLandM5RegionCoord Region;
			const auto& Plan = Candidate.GetPlan();
			bBuilt = Seal->Matches(*Baseline, *Influence, *Budget) && Candidate.UsesBudget(*Budget);
			TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Field;
			Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), Influence.ToSharedRef(),
				Budget.ToSharedRef(), Seal.ToSharedRef(), Never, Field);
			bSealedBinding = Result.Code == EVoxelLandM5ErrorCode::None && Field && Field->GetSeal() == Seal.Get();
			if (bSealedBinding)
			{
				TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Job;
				FVoxelLandChunkKey Key;
				Key.Coordinate = FIntVector::ZeroValue;
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
				if (FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Job)
					&& !Baseline->GetGenerator().GenerateChunkCancellable(Key, *Worker, Chunk, Never) && Chunk)
				{
					const auto Previous = Chunk;
					Result =
						VoxelLand::M5::FinalField::EvaluateChunk(*Field, Key, *Worker, Job.ToSharedRef(), Never, Chunk);
					bSealedNetwork = Plan.Chambers.Num() == 2 && Plan.Connections.Num() == 1 && Plan.Segments.Num() > 0
								  && Result.Code == EVoxelLandM5ErrorCode::None && Chunk && Chunk != Previous
								  && Chunk->Key.Coordinate == Key.Coordinate && Chunk->Key.LOD == Key.LOD;
				}
			}
			Field.Reset();
			bRegion = Plan.Region.X == 0 && Plan.Region.Y == 0
				   && Candidate.MatchesSource(Expected, Snapshot->Base.Seed, Region);
			bGate = Plan.Bounds.MinInclusive.X == Gate.MinInclusive.X
				 && Plan.Bounds.MinInclusive.Y == Gate.MinInclusive.Y
				 && Plan.Bounds.MinInclusive.Z == Gate.MinInclusive.Z
				 && Plan.Bounds.MaxExclusive.X == Gate.MaxExclusive.X
				 && Plan.Bounds.MaxExclusive.Y == Gate.MaxExclusive.Y
				 && Plan.Bounds.MaxExclusive.Z == Gate.MaxExclusive.Z;
			TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Independent;
			Result = VoxelLand::M5::CandidatePlanner::Build(
				Expected, Snapshot->Base.Seed, Region, Budget.ToSharedRef(), Never, Independent);
			bDeterministic = Result.Code == EVoxelLandM5ErrorCode::None && Independent && Plan.Chambers.Num() == 1
						  && Independent->GetPlan().Chambers.Num() == 1
						  && Plan.Chambers[0].CenterCm.X == Independent->GetPlan().Chambers[0].CenterCm.X
						  && Plan.Chambers[0].CenterCm.Y == Independent->GetPlan().Chambers[0].CenterCm.Y
						  && Plan.Chambers[0].CenterCm.Z == Independent->GetPlan().Chambers[0].CenterCm.Z;
			Independent.Reset();
			auto Prior = Seal;
			Result = FVoxelLandM5FiniteContributorSeal::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(),
				ForeignBudget.ToSharedRef(), *Worker, 100000, Never, Seal);
			bForeignBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Seal == Prior;
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> OtherDefinition;
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> OtherInfluence;
			if (FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, OtherDefinition).Code
					== EVoxelLandM5ErrorCode::None
				&& OtherDefinition
				&& FVoxelLandM5InfluenceSet::BuildFinite(
					   OtherDefinition.ToSharedRef(), Budget.ToSharedRef(), Never, OtherInfluence)
						   .Code
					   == EVoxelLandM5ErrorCode::None
				&& OtherInfluence)
			{
				Result = FVoxelLandM5FiniteContributorSeal::Build(Baseline.ToSharedRef(), OtherInfluence.ToSharedRef(),
					Budget.ToSharedRef(), *Worker, 100000, Never, Seal);
				bForeignDefinition = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Seal == Prior;
			}
			Result = FVoxelLandM5FiniteContributorSeal::Build(
				Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 0, Never, Seal);
			bLimit = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Seal == Prior;
			Result = FVoxelLandM5FiniteContributorSeal::Build(
				Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 100000,
				[] { return true; }, Seal);
			bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Seal == Prior;
			Async(EAsyncExecution::ThreadPool, [&] {
				auto OtherOutput = Seal;
				const auto Wrong = FVoxelLandM5FiniteContributorSeal::Build(Baseline.ToSharedRef(),
					Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 100000, Never, OtherOutput);
				bWrongThread = Wrong.Code == EVoxelLandM5ErrorCode::InvalidInput && OtherOutput == Prior;
			}).Wait();
			OtherInfluence.Reset();
			OtherDefinition.Reset();
			Worker.Reset();
			Influence.Reset();
			Definition.Reset();
			Baseline.Reset();
			Capture.Reset();
			int64 Held = 0, Peak = 0, Work = 0, Released = 0, PeakAfter = 0, WorkAfter = 0;
			Budget->GetUsage(Held, Peak, Work);
			bRetained = Seal->GetCandidate().GetPlan().Chambers.Num() == 1 && Held > 0;
			Prior.Reset();
			Seal.Reset();
			Budget->GetUsage(Released, PeakAfter, WorkAfter);
			bRetained &= Released < Held && WorkAfter == Work;
		}).Wait();
		Capture.Reset();
	}
	TestTrue(FString::Printf(TEXT("real contributor build: %s"), *Failure), bBuilt);
	TestTrue(TEXT("sealed field retains exact contributor owner"), bSealedBinding);
	TestTrue(TEXT("sealed network produces scratch chunk"), bSealedNetwork);
	TestTrue(TEXT("canonical finite region"), bRegion);
	TestTrue(TEXT("candidate uses exact declared gate"), bGate);
	TestTrue(TEXT("same source and gate generate same candidate"), bDeterministic);
	TestTrue(TEXT("foreign budget preserves prior seal"), bForeignBudget);
	TestTrue(TEXT("equal foreign definition preserves prior seal"), bForeignDefinition);
	TestTrue(TEXT("wrong-thread worker preserves prior seal"), bWrongThread);
	TestTrue(TEXT("query limit rejects atomically"), bLimit);
	TestTrue(TEXT("cancellation preserves prior seal"), bCancelled);
	TestTrue(TEXT("seal retains admitted owners and releases lease"), bRetained);
	TestTrue(TEXT("smoothed entrance rejects unproven complete column"), bSmoothRejected);
	TestTrue(TEXT("oversized entrance box rejects without partial seal"), bTallRejected);
	TestTrue(TEXT("slope raw dependency reaches aligned grid edge"), bSlopeBounds);
	TestTrue(TEXT("post-filter slope box fits"), bSlopeBox);
	TestTrue(TEXT("double-expanded slope box rejects atomically"), bSlopeRawRejected);
	TestTrue(TEXT("aligned slope boundary reaches entrance planner"), bSlopePlanner);
	return bBuilt && bSealedBinding && bSealedNetwork && bRegion && bGate && bDeterministic && bForeignBudget
		&& bForeignDefinition && bWrongThread && bLimit && bCancelled && bRetained && bSmoothRejected && bTallRejected
		&& bSlopeBounds && bSlopeBox && bSlopeRawRejected && bSlopePlanner;
}
#endif
