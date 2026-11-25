#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Data/Structs/VoxelLandM5ResourceLimits.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Generation/VoxelLandM5EvaluateChunk.h"
#include "Generation/VoxelLandM5FieldInputs.h"
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

namespace
{
bool MakeEvaluationControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
void ApplyEvaluationControl(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Owner);
	Graph->SourceVersion = Source.Source.SourceVersion; Graph->Parameters = Source.Source.Parameters;
	Graph->Nodes = Source.Source.Nodes; Graph->Links = Source.Source.Links; Graph->CompiledProgram = Source.Payload;
	Target.Graph = Graph; Target.CurveKeys = Source.CurveKeys; Target.CookedCurve = Source.CookedCurve;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5EvaluateChunkTest, "VoxelLand.M5.EvaluateChunk.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5EvaluateChunkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread())) return false;
	FVoxelLandM4ControlSnapshot Control; FString Error; bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeEvaluationControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl)) return false;
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	ApplyEvaluationControl(*Config.Get(), Config->Continental, Control);
	ApplyEvaluationControl(*Config.Get(), Config->Mountain, Control);
	ApplyEvaluationControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental; Asset->Common.Mountain = Config->Mountain; Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = -1; Asset->Common.BaseHeightQ8 = 2050 * 256;
	Asset->Common.Caves.StageId = FGuid(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Pockets.Salt = 0x0102030405060708ll;
	Asset->Common.Caves.Pockets.ThresholdQ16 = 0;
	Asset->Common.Caves.Pockets.MinDepthCm = 0; Asset->Common.Caves.Pockets.MaxDepthCm = 10000;
	Asset->Finite.bGlobalErosion = false; Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800; Asset->Finite.GridSpacingCm = 6400;
	// This bounded source still includes the full vertical pocket column and
	// all read halos, while exercising the actual default per-job work cap.
	Asset->Finite.MinZCm = -6400; Asset->Finite.MaxZCm = 6401;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("EvaluateChunk fixture requires the M4 process budget to be 256 MiB")); return false;
	}
	const auto Never = [] { return false; };
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result; if (Generator.CreateWorker(Worker)) Result.Code = EVoxelLandM5ErrorCode::InvalidInput; return Result;
	};
	bool bParity = false, bPocket = false, bGate = false, bSeam = false, bRetained = false, bSourceBudget = false;
	bool bWrongSource = false, bWrongThread = false, bCancel = false, bNetwork = false, bUncovered = false;
	bool bJobLease = false, bExhausted = false, bAliasedJob = false, bZeroUncovered = false, bDefaultActive = false;
	bool bBroadDefault = false, bPeriod16Exhausted = false;
	const FVoxelLandM5ResourceLimits DefaultLimits;
	const int32 DefaultPocketPeriod = Asset->Common.Caves.Pockets.LatticeSpacingCm;
	FString Failure;
	for (int32 Mode = 0; Mode < 5; ++Mode)
	{
		Asset->Common.Caves.bEnabled = Mode == 2;
		Asset->Common.Caves.Pockets.bEnabled = Mode != 2;
		Asset->Common.Caves.Pockets.AmplitudeCm = Mode == 0 ? 0 : 400;
		// Mode 3 uses the authored default period across the entire horizontal
		// chunk footprint; mode 4 exercises the sparse period-16 fallback.
		Asset->Common.Caves.Pockets.LatticeSpacingCm = Mode == 3 ? DefaultPocketPeriod : 16;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, ForeignBudget;
		if (!FVoxelLandM5Budget::TryCreate(DefaultLimits.MaxBytes, DefaultLimits.MaxValidationWork, Budget)
			|| !FVoxelLandM5Budget::TryCreate(DefaultLimits.MaxBytes, DefaultLimits.MaxValidationWork, ForeignBudget)) return false;
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), Capture);
		if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message), Captured.Code == EVoxelLandM5ErrorCode::None && Capture)) return false;
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
			auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
			FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
			Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -500;
			Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 500;
			if (Mode >= 3)
			{
				Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -100;
				Gate.MaxExclusive.X = Gate.MaxExclusive.Y = 3301; Gate.MaxExclusive.Z = 2900;
			}
			FVoxelLandM5DefinitionInputs DefinitionInputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			auto Result = FVoxelLandM5DefinitionValidator::Validate(DefinitionInputs, Budget.ToSharedRef(), Never, Definition);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition) { Failure = Result.Message; return; }
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
			Result = FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Budget.ToSharedRef(), Never, Influence);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence) { Failure = Result.Message; return; }
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
			Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(Capture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Baseline) { Failure = Result.Message; return; }
			TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Inputs;
			Result = FVoxelLandM5FieldInputs::Bind(Baseline.ToSharedRef(), Influence.ToSharedRef(), Budget.ToSharedRef(), Never, Inputs);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Inputs) { Failure = Result.Message; return; }
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) { Failure = TEXT("worker creation failed"); return; }
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Job;
			if (!FVoxelLandM5Budget::TryCreate(DefaultLimits.MaxBytes, DefaultLimits.MaxValidationWork, Job)) return;
			FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(0, 0, 0);
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Ordinary;
			if (Baseline->GetGenerator().GenerateChunkCancellable(Key, *Worker, Ordinary, Never) || !Ordinary) return;
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = Ordinary;
			if (Mode == 2)
			{
				Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), Never, Chunk);
				bNetwork = Result.Code == EVoxelLandM5ErrorCode::Unproven && Chunk == Ordinary; return;
			}
			int64 LiveBefore = 0, PeakBefore = 0, WorkBefore = 0;
			Budget->GetUsage(LiveBefore, PeakBefore, WorkBefore);
			Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), Never, Chunk);
			Failure = Result.Message;
			if (Mode == 4)
			{
				int64 MaxBytes = 0, MaxWork = 0; Job->GetLimits(MaxBytes, MaxWork);
				bPeriod16Exhausted = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded
					&& Result.Stage == TEXT("M5.FinalField") && Result.Message == TEXT("Pocket sampler work budget exhausted")
					&& Snapshot->Caves.Pockets.LatticeSpacingCm == 16 && Job.Get() != Budget.Get()
					&& MaxBytes == DefaultLimits.MaxBytes && MaxWork == 10000000 && Chunk == Ordinary;
				return;
			}
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Chunk) return;
			int64 LiveAfter = 0, PeakAfter = 0, WorkAfter = 0; Budget->GetUsage(LiveAfter, PeakAfter, WorkAfter);
			if (Mode == 0)
			{
				bParity = Chunk->Density == Ordinary->Density && Chunk->Hash == Ordinary->Hash;
				bSourceBudget = LiveBefore == LiveAfter && WorkBefore == WorkAfter;
				auto Prior = Chunk;
				TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TinyJob;
				if (!FVoxelLandM5Budget::TryCreate(1, 1, TinyJob)) return;
				Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, TinyJob.ToSharedRef(), Never, Chunk);
				bExhausted = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Chunk == Prior;
				Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Budget.ToSharedRef(), Never, Chunk);
				bAliasedJob = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Chunk == Prior;
				TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Other = Inputs;
				Result = FVoxelLandM5FieldInputs::Bind(Baseline.ToSharedRef(), Influence.ToSharedRef(), ForeignBudget.ToSharedRef(), Never, Other);
				bWrongSource = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Other == Inputs;
				Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), [] { return true; }, Chunk);
				bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Chunk == Prior;
				FVoxelLandChunkKey OutsideZero; OutsideZero.Coordinate = FIntVector(2, 0, 0);
				Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, OutsideZero, *Worker, Job.ToSharedRef(), Never, Chunk);
				bZeroUncovered = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Chunk == Prior;
				Async(EAsyncExecution::ThreadPool, [&] {
					auto OtherChunk = Chunk;
					const auto Wrong = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), Never, OtherChunk);
					bWrongThread = Wrong.Code == EVoxelLandM5ErrorCode::InvalidInput && OtherChunk == Prior;
				}).Wait();
				Worker.Reset(); Inputs.Reset(); Influence.Reset(); Definition.Reset(); Baseline.Reset(); Ordinary.Reset(); Capture.Reset();
				bRetained = Chunk && Chunk->Density.Num() == FVoxelLandChunkData::SampleCount && Chunk->Hash == Prior->Hash;
				int64 LiveHeld = 0, PeakHeld = 0, WorkHeld = 0, LiveReleased = 0, PeakReleased = 0, WorkReleased = 0;
				Job->GetUsage(LiveHeld, PeakHeld, WorkHeld);
				Prior.Reset(); Chunk.Reset();
				Job->GetUsage(LiveReleased, PeakReleased, WorkReleased);
				bJobLease = LiveHeld > 0 && LiveReleased == 0 && WorkHeld == WorkReleased;
				return;
			}
			if (Mode == 3)
			{
				int32 Changed = 0;
				for (int32 I = 0; I < Chunk->Density.Num(); ++I) Changed += Chunk->Density[I] != Ordinary->Density[I] ? 1 : 0;
				int64 MaxBytes = 0, MaxWork = 0, Live = 0, Peak = 0, Used = 0;
				Job->GetLimits(MaxBytes, MaxWork); Job->GetUsage(Live, Peak, Used);
				bBroadDefault = Chunk->Density.Num() == FVoxelLandChunkData::SampleCount && Changed > 1000
					&& Snapshot->Caves.Pockets.LatticeSpacingCm == 2000 && Snapshot->Caves.Limits.MaxValidationWork == 10000000
					&& MaxBytes == DefaultLimits.MaxBytes && MaxWork == 10000000 && Used > 0 && Used <= MaxWork
					&& Job.Get() != Budget.Get() && LiveBefore == LiveAfter && WorkBefore == WorkAfter;
				return;
			}
			bPocket = Chunk->Density[1 + 35 * (1 + 35)] > Ordinary->Density[1 + 35 * (1 + 35)];
			int64 JobMaxBytes = 0, JobMaxWork = 0, JobLive = 0, JobPeak = 0, JobUsed = 0;
			Job->GetLimits(JobMaxBytes, JobMaxWork); Job->GetUsage(JobLive, JobPeak, JobUsed);
			bDefaultActive = bPocket && Job.Get() != Budget.Get() && DefaultLimits.MaxValidationWork == 10000000
				&& Snapshot->Caves.Limits.MaxValidationWork == DefaultLimits.MaxValidationWork
				&& JobMaxBytes == DefaultLimits.MaxBytes && JobMaxWork == DefaultLimits.MaxValidationWork
				&& JobUsed > 0 && JobUsed <= JobMaxWork && LiveBefore == LiveAfter && WorkBefore == WorkAfter;
			bGate = true;
			for (int32 Z = 0; Z < 35; ++Z) for (int32 Y = 0; Y < 35; ++Y) for (int32 X = 0; X < 35; ++X)
			{
				const int32 I = X + 35 * (Y + 35 * Z);
				const int32 CX = (X - 1) * 100, CY = (Y - 1) * 100, CZ = (Z - 1) * 100;
				if (CX < -500 || CX >= 500 || CY < -500 || CY >= 500 || CZ < -500 || CZ >= 500)
					bGate &= Chunk->Density[I] == Ordinary->Density[I];
			}
			FVoxelLandChunkKey Adjacent; Adjacent.Coordinate = FIntVector(-1, 0, 0);
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> OtherChunk;
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> AdjacentJob;
			if (!FVoxelLandM5Budget::TryCreate(DefaultLimits.MaxBytes, DefaultLimits.MaxValidationWork, AdjacentJob)) return;
			Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Adjacent, *Worker, AdjacentJob.ToSharedRef(), Never, OtherChunk);
			bSeam = Result.Code == EVoxelLandM5ErrorCode::None && OtherChunk;
			if (bSeam) for (int32 Z = 0; Z < 35; ++Z) for (int32 Y = 0; Y < 35; ++Y) for (int32 X = 0; X < 3; ++X)
				bSeam &= OtherChunk->Density[(X + 32) + 35 * (Y + 35 * Z)] == Chunk->Density[X + 35 * (Y + 35 * Z)];
			FVoxelLandChunkKey Outside; Outside.Coordinate = FIntVector(2, 0, 0);
			const auto Prior = Chunk;
			Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Outside, *Worker, Job.ToSharedRef(), Never, Chunk);
			bUncovered = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Chunk == Prior;
		}).Wait();
		Capture.Reset();
	}
	TestTrue(FString::Printf(TEXT("zero-amplitude exact M4 parity: %s"), *Failure), bParity);
	TestTrue(TEXT("job evaluation does not consume source admission"), bSourceBudget);
	TestTrue(TEXT("job lease retained through final output release"), bJobLease);
	TestTrue(TEXT("exhausted job budget preserves output"), bExhausted);
	TestTrue(TEXT("source budget cannot serve as job budget"), bAliasedJob);
	TestTrue(TEXT("output remains valid after source owners release"), bRetained);
	TestTrue(TEXT("foreign source budget preserves inputs"), bWrongSource);
	TestTrue(TEXT("wrong-thread worker preserves output"), bWrongThread);
	TestTrue(TEXT("cancellation preserves output"), bCancel);
	TestTrue(FString::Printf(TEXT("actual pocket contribution: %s"), *Failure), bPocket);
	TestTrue(TEXT("active pocket uses the default independent 10M job cap"), bDefaultActive);
	TestTrue(FString::Printf(TEXT("broad default-period active pocket uses default cap: %s"), *Failure), bBroadDefault);
	TestTrue(TEXT("broad sparse period-16 pocket exhausts default job atomically"), bPeriod16Exhausted);
	TestTrue(TEXT("outside declared gate retains exact baseline"), bGate);
	TestTrue(TEXT("negative-X stored overlap identical"), bSeam);
	TestTrue(TEXT("uncovered pocket key rejects atomically"), bUncovered);
	TestTrue(TEXT("uncovered zero-amplitude key rejects atomically"), bZeroUncovered);
	TestTrue(TEXT("network without complete contributors rejects atomically"), bNetwork);
	return bParity && bSourceBudget && bJobLease && bExhausted && bAliasedJob && bRetained && bWrongSource && bWrongThread && bCancel && bPocket && bDefaultActive && bBroadDefault && bPeriod16Exhausted && bGate && bSeam && bUncovered && bZeroUncovered && bNetwork;
}
#endif
