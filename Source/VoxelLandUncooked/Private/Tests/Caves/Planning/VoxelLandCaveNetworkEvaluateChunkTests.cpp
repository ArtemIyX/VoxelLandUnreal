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
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Generation/Caves/Planning/VoxelLandCaveEvaluateChunk.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldInputs.h"
#include "Generation/Caves/Validation/VoxelLandCaveFiniteContributorSeal.h"
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

namespace
{
bool MakeNetworkEvaluationControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
void ApplyNetworkEvaluationControl(
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5NetworkEvaluateChunkTest, "VoxelLand.M5.NetworkEvaluateChunk.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5NetworkEvaluateChunkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeNetworkEvaluationControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	ApplyNetworkEvaluationControl(*Config.Get(), Config->Continental, Control);
	ApplyNetworkEvaluationControl(*Config.Get(), Config->Mountain, Control);
	ApplyNetworkEvaluationControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorldSource> Asset(NewObject<UVoxelLandFiniteWorldSource>());
	Asset->Common.Continental = Config->Continental;
	Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345;
	Asset->Common.BaseHeightQ8 = 5050 * 256;
	Asset->Common.Caves.bEnabled = true;
	Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Topology.MinChambers = Asset->Common.Caves.Topology.MaxChambers = 2;
	Asset->Common.Caves.Topology.MinChamberRadiusCm = Asset->Common.Caves.Topology.MaxChamberRadiusCm = 100;
	Asset->Common.Caves.Topology.MinTunnelRadiusCm = Asset->Common.Caves.Topology.MaxTunnelRadiusCm = 100;
	Asset->Common.Caves.Topology.MinLoops = Asset->Common.Caves.Topology.MaxLoops = 0;
	Asset->Common.Caves.Topology.EntranceCount = 0;
	Asset->Common.Caves.Pockets.LatticeSpacingCm = 16;
	Asset->Common.Caves.Pockets.ThresholdQ16 = 0;
	Asset->Common.Caves.Pockets.MinDepthCm = 0;
	Asset->Common.Caves.Pockets.MaxDepthCm = 10000;
	Asset->Common.Caves.Limits.MaxValidationWork = 100000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	Asset->Finite.MinZCm = -6400;
	Asset->Finite.MaxZCm = 12801;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Network evaluator fixture requires the M4 process budget to be 256 MiB"));
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
	bool bNetwork = false, bCombined = false, bProtected = false, bGate = true, bSeam = true, bTopology = true;
	bool bNoSeal = false, bForeign = false, bForeignSeal = false, bForeignBudget = false, bCancelled = false,
		 bJobLease = false, bSourceBudget = false;
	TArray<int16> NetworkDensity;
	FString Failure;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		Asset->Common.Caves.Pockets.bEnabled = Mode == 1;
		Asset->Common.Caves.Pockets.AmplitudeCm = 400;
		Asset->Common.Caves.Protection.bProtectAll = Mode == 2;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, ForeignBudget, Job, SeamJob;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Budget)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, ForeignBudget)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Job)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, SeamJob))
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
			Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -500;
			Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 500;
			FVoxelLandM5DefinitionInputs DefinitionInputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			auto Result =
				FVoxelLandM5DefinitionValidator::Validate(DefinitionInputs, Budget.ToSharedRef(), Never, Definition);
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
			Result = FVoxelLandM5FiniteContributorSeal::Build(
				Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 100000, Never, Seal);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Seal)
			{
				Failure = Result.Message;
				bTopology = false;
				return;
			}
			const auto& Plan = Seal->GetCandidate().GetPlan();
			const bool bConnectedPair =
				Plan.Chambers.Num() == 2 && Plan.Connections.Num() == 1 && Plan.Segments.Num() == 1
				&& Plan.Chambers[0].bHasId && Plan.Chambers[1].bHasId && Plan.Chambers[0].Id != Plan.Chambers[1].Id
				&& Plan.Connections[0].bHasId && Plan.Connections[0].Start.bValid && Plan.Connections[0].End.bValid
				&& ((Plan.Connections[0].Start.Id == Plan.Chambers[0].Id
						&& Plan.Connections[0].End.Id == Plan.Chambers[1].Id)
					|| (Plan.Connections[0].Start.Id == Plan.Chambers[1].Id
						&& Plan.Connections[0].End.Id == Plan.Chambers[0].Id))
				&& Plan.Segments[0].bHasConnectionId && Plan.Segments[0].ConnectionId == Plan.Connections[0].Id;
			bTopology &= bConnectedPair;
			if (!bConnectedPair)
			{
				return;
			}
			TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Inputs;
			Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), Influence.ToSharedRef(),
				Budget.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Inputs)
			{
				Failure = Result.Message;
				return;
			}
			FVoxelLandChunkKey Key;
			Key.Coordinate = FIntVector::ZeroValue;
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Ordinary;
			if (Baseline->GetGenerator().GenerateChunkCancellable(Key, *Worker, Ordinary, Never) || !Ordinary)
			{
				return;
			}
			auto Output = Ordinary;
			if (Mode == 0)
			{
				TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Unsealed;
				if (FVoxelLandM5FieldInputs::Bind(
						Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), Never, Unsealed)
							.Code
						!= EVoxelLandM5ErrorCode::None
					|| !Unsealed)
				{
					return;
				}
				Result =
					VoxelLand::M5::FinalField::EvaluateChunk(*Unsealed, Key, *Worker, Job.ToSharedRef(), Never, Output);
				bNoSeal = Result.Code == EVoxelLandM5ErrorCode::Unproven && Output == Ordinary;
				const auto PriorInputs = Inputs;
				Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), Influence.ToSharedRef(),
					ForeignBudget.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
				bForeignBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Inputs == PriorInputs;
				TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> IndependentInfluence;
				TSharedPtr<const FVoxelLandM5FiniteContributorSeal, ESPMode::ThreadSafe> ForeignSeal;
				if (FVoxelLandM5InfluenceSet::BuildFinite(
						Definition.ToSharedRef(), Budget.ToSharedRef(), Never, IndependentInfluence)
							.Code
						== EVoxelLandM5ErrorCode::None
					&& IndependentInfluence
					&& FVoxelLandM5FiniteContributorSeal::Build(Baseline.ToSharedRef(),
						   IndependentInfluence.ToSharedRef(), Budget.ToSharedRef(), *Worker, 100000, Never,
						   ForeignSeal)
							   .Code
						   == EVoxelLandM5ErrorCode::None
					&& ForeignSeal)
				{
					Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), Influence.ToSharedRef(),
						Budget.ToSharedRef(), ForeignSeal.ToSharedRef(), Never, Inputs);
					bForeignSeal = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Inputs == PriorInputs;
				}
				TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> OtherDefinition;
				TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> OtherInfluence;
				if (FVoxelLandM5DefinitionValidator::Validate(
						DefinitionInputs, Budget.ToSharedRef(), Never, OtherDefinition)
							.Code
						== EVoxelLandM5ErrorCode::None
					&& OtherDefinition
					&& FVoxelLandM5InfluenceSet::BuildFinite(
						   OtherDefinition.ToSharedRef(), Budget.ToSharedRef(), Never, OtherInfluence)
							   .Code
						   == EVoxelLandM5ErrorCode::None
					&& OtherInfluence)
				{
					Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), OtherInfluence.ToSharedRef(),
						Budget.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
					bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Inputs == PriorInputs;
				}
			}
			int64 LiveBefore = 0, PeakBefore = 0, WorkBefore = 0;
			Budget->GetUsage(LiveBefore, PeakBefore, WorkBefore);
			Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), Never, Output);
			Failure = Result.Message;
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Output)
			{
				return;
			}
			int64 LiveAfter = 0, PeakAfter = 0, WorkAfter = 0;
			Budget->GetUsage(LiveAfter, PeakAfter, WorkAfter);
			bSourceBudget |=
				Mode == 0 && Job.Get() != Budget.Get() && LiveBefore == LiveAfter && WorkBefore == WorkAfter;
			int32 Changed = 0;
			for (int32 Z = 0; Z < 35; ++Z)
			{
				for (int32 Y = 0; Y < 35; ++Y)
				{
					for (int32 X = 0; X < 35; ++X)
					{
						const int32 I = X + 35 * (Y + 35 * Z);
						const int32 CX = (X - 1) * 100, CY = (Y - 1) * 100, CZ = (Z - 1) * 100;
						Changed += Output->Density[I] != Ordinary->Density[I] ? 1 : 0;
						if (CX < -500 || CX >= 500 || CY < -500 || CY >= 500 || CZ < -500 || CZ >= 500)
						{
							bGate &= Output->Density[I] == Ordinary->Density[I];
						}
					}
				}
			}
			if (Mode == 0)
			{
				bNetwork = Changed > 0;
				NetworkDensity = Output->Density;
			}
			if (Mode == 1 && NetworkDensity.Num() == Output->Density.Num())
			{
				for (int32 I = 0; I < NetworkDensity.Num(); ++I)
				{
					bCombined |= Output->Density[I] > NetworkDensity[I];
				}
			}
			if (Mode == 2)
			{
				bProtected = Changed == 0 && Output->Hash == Ordinary->Hash;
			}
			FVoxelLandChunkKey Neighbor;
			Neighbor.Coordinate = FIntVector(-1, 0, 0);
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Adjacent;
			Result = VoxelLand::M5::FinalField::EvaluateChunk(
				*Inputs, Neighbor, *Worker, SeamJob.ToSharedRef(), Never, Adjacent);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Adjacent)
			{
				Failure = Result.Message;
				bSeam = false;
				return;
			}
			for (int32 Z = 0; Z < 35; ++Z)
			{
				for (int32 Y = 0; Y < 35; ++Y)
				{
					for (int32 X = 0; X < 3; ++X)
					{
						bSeam &=
							Adjacent->Density[(X + 32) + 35 * (Y + 35 * Z)] == Output->Density[X + 35 * (Y + 35 * Z)];
					}
				}
			}
			if (Mode == 0)
			{
				auto Prior = Output;
				Result = VoxelLand::M5::FinalField::EvaluateChunk(
					*Inputs, Key, *Worker, Job.ToSharedRef(), [] { return true; }, Output);
				bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Output == Prior;
				int64 Held = 0, JobPeak = 0, JobWork = 0, Released = 0, JobPeakAfter = 0, JobWorkAfter = 0;
				Job->GetUsage(Held, JobPeak, JobWork);
				Prior.Reset();
				Output.Reset();
				Job->GetUsage(Released, JobPeakAfter, JobWorkAfter);
				bJobLease = Held > 0 && Released == 0 && JobWork == JobWorkAfter;
			}
		}).Wait();
		Capture.Reset();
	}
	TestTrue(TEXT("seal contains two distinct chambers joined by one connection and segment"), bTopology);
	TestTrue(FString::Printf(TEXT("real sealed network changes density: %s"), *Failure), bNetwork);
	TestTrue(TEXT("combined pockets add to sealed network"), bCombined);
	TestTrue(TEXT("protected samples retain baseline"), bProtected);
	TestTrue(TEXT("outside exact gate retains baseline"), bGate);
	TestTrue(TEXT("negative-X chunk overlap remains identical"), bSeam);
	TestTrue(TEXT("missing seal rejects without output change"), bNoSeal);
	TestTrue(TEXT("foreign definition rejects sealed binding"), bForeign);
	TestTrue(TEXT("foreign seal rejects sealed binding"), bForeignSeal);
	TestTrue(TEXT("foreign budget rejects sealed binding"), bForeignBudget);
	TestTrue(TEXT("source and job accounting remain distinct"), bSourceBudget);
	TestTrue(TEXT("job lease remains until final output release"), bJobLease);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	return bTopology && bNetwork && bCombined && bProtected && bGate && bSeam && bNoSeal && bForeign && bForeignSeal
		&& bForeignBudget && bSourceBudget && bJobLease && bCancelled;
}
#endif
