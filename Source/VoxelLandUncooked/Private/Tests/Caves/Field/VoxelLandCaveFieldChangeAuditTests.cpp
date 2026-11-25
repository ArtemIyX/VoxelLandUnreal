#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Data/Caves/Field/VoxelLandCaveLatticePoint.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Generation/Caves/Planning/VoxelLandCaveEvaluateChunk.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldInputs.h"
#include "Generation/Caves/Field/VoxelLandCaveFieldChangeAudit.h"
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

class FVoxelLandM5FieldAuditFixture final
{
public:
	static bool NeverCancel() { return false; }
	static bool AlwaysCancel() { return true; }
	static FVoxelLandM5Diagnostic CreateWorker(const FVoxelLandGenerator& Generator,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TUniquePtr<FVoxelLandWorkerContext>& Worker)
	{
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	}
	static void CountDifferences(const FVoxelLandChunkData& Base, const FVoxelLandChunkData& Final, int64& Compared,
		int64& Changed, bool& bFoundSameSign)
	{
		for (int32 I = 0; I < Final.Density.Num(); ++I)
		{
			const int16 Before = Base.Density[I];
			const int16 After = Final.Density[I];
			++Compared;
			if (Before != After)
			{
				++Changed;
				bFoundSameSign |= (Before < 0 && After < 0) || (Before > 0 && After > 0);
			}
		}
	}
	static bool HasChangedOverlap(const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right,
		const FVoxelLandChunkData& LeftBase, const FVoxelLandChunkData& RightBase)
	{
		bool bFound = false;
		for (int32 Z = 0; Z < 35; ++Z)
		{
			for (int32 Y = 0; Y < 35; ++Y)
			{
				for (int32 X = 0; X < 3; ++X)
				{
					const int32 LeftIndex = X + 32 + 35 * (Y + 35 * Z);
					const int32 RightIndex = X + 35 * (Y + 35 * Z);
					bFound |= Left.Density[LeftIndex] == Right.Density[RightIndex]
						   && LeftBase.Density[LeftIndex] == RightBase.Density[RightIndex]
						   && Left.Density[LeftIndex] != LeftBase.Density[LeftIndex];
				}
			}
		}
		return bFound;
	}
	static bool MakeControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
	{
		FVoxelLandGraphNodeRecord Constant;
		FVoxelLandGraphNodeRecord Output;
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
		FVoxelLandM4CurveAuthoringKey First;
		FVoxelLandM4CurveAuthoringKey Last;
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
	static void ApplyControl(
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

struct FVoxelLandM5FieldAuditControlTask final
{
	FVoxelLandM4ControlSnapshot& Control;
	FString& Error;
	bool& bResult;
	void operator()() const { bResult = FVoxelLandM5FieldAuditFixture::MakeControl(Control, Error); }
};

struct FVoxelLandM5FieldAuditWrongThreadTask final
{
	TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe>& Field;
	FVoxelLandWorkerContext& Worker;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget;
	TSharedPtr<const FVoxelLandM5FieldChangeAudit, ESPMode::ThreadSafe>& Prior;
	bool& bRejected;
	void operator()() const
	{
		TSharedPtr<const FVoxelLandM5FieldChangeAudit, ESPMode::ThreadSafe> Other = Prior;
		const FVoxelLandM5Diagnostic Result = FVoxelLandM5FieldChangeAudit::Build(
			Field.ToSharedRef(), Worker, Budget.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Other);
		bRejected = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Other == Prior;
	}
};

struct FVoxelLandM5FieldAuditWorkerTask final
{
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& Capture;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Source;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& AuditBudget;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& EvaluationBudget;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Tiny;
	bool bProtectAll;
	bool& bSuccess;
	bool& bCounts;
	bool& bSameSign;
	bool& bHalo;
	bool& bCancelled;
	bool& bExhausted;
	bool& bWrongThread;
	bool& bSourceBudget;
	bool& bMissingOwner;
	bool& bLease;
	FString& Failure;

	void operator()() const
	{
		TSharedRef<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
		TSharedRef<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe> Registry =
			MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
		Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -500;
		Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 500;
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
		FVoxelLandM5Diagnostic Result = FVoxelLandM5DefinitionValidator::Validate(
			Inputs, Source.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Definition);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
		Result = FVoxelLandM5InfluenceSet::BuildFinite(
			Definition.ToSharedRef(), Source.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Influence);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
		Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(Capture.ToSharedRef(), Definition.ToSharedRef(),
			FVoxelLandM5FieldAuditFixture::CreateWorker, FVoxelLandM5FieldAuditFixture::NeverCancel, Baseline);
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
		Result = FVoxelLandM5FiniteContributorSeal::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(),
			Source.ToSharedRef(), *Worker, 100000, FVoxelLandM5FieldAuditFixture::NeverCancel, Seal);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Seal)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Field;
		Result = FVoxelLandM5FieldInputs::BindSealed(Baseline.ToSharedRef(), Influence.ToSharedRef(),
			Source.ToSharedRef(), Seal.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Field);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Field)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5FieldChangeAudit, ESPMode::ThreadSafe> Audit;
		Result = FVoxelLandM5FieldChangeAudit::Build(
			Field.ToSharedRef(), *Worker, AuditBudget.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Audit);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Audit)
		{
			Failure = Result.Message;
			return;
		}
		bSuccess = &Audit->GetInputs() == Field.Get();
		const FVoxelLandM5InfluenceRange& Coverage = Influence->GetCoverage();
		int64 Compared = 0;
		int64 Changed = 0;
		bool bFoundSameSign = false;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ZeroChunk;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> LeftChunk;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ZeroBase;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> LeftBase;
		for (int32 Z = Coverage.MinChunk.Z; Z < Coverage.MaxChunkExclusive.Z; ++Z)
		{
			for (int32 Y = Coverage.MinChunk.Y; Y < Coverage.MaxChunkExclusive.Y; ++Y)
			{
				for (int32 X = Coverage.MinChunk.X; X < Coverage.MaxChunkExclusive.X; ++X)
				{
					FVoxelLandChunkKey Key;
					Key.Coordinate = FIntVector(X, Y, Z);
					TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Base;
					TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Final;
					if (Baseline->GetGenerator().GenerateChunkCancellable(
							Key, *Worker, Base, FVoxelLandM5FieldAuditFixture::NeverCancel)
						|| !Base)
					{
						return;
					}
					Result = VoxelLand::M5::FinalField::EvaluateChunk(*Field, Key, *Worker,
						EvaluationBudget.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Final);
					if (Result.Code != EVoxelLandM5ErrorCode::None || !Final)
					{
						Failure = Result.Message;
						return;
					}
					FVoxelLandM5FieldAuditFixture::CountDifferences(*Base, *Final, Compared, Changed, bFoundSameSign);
					if (Key.Coordinate == FIntVector::ZeroValue)
					{
						ZeroChunk = Final;
						ZeroBase = Base;
					}
					if (Key.Coordinate == FIntVector(-1, 0, 0))
					{
						LeftChunk = Final;
						LeftBase = Base;
					}
				}
			}
		}
		bCounts = Audit->GetComparedSamples() == Compared && Audit->GetChangedSamples() == Changed && Compared > 0
			   && (bProtectAll ? Changed == 0 : Changed > 0) && Audit->GetExaminedEdges() == Changed * 54;
		bSameSign = bProtectAll || bFoundSameSign;
		if (bProtectAll)
		{
			bHalo = true;
		}
		else if (ZeroChunk && LeftChunk && ZeroBase && LeftBase)
		{
			bHalo = FVoxelLandM5FieldAuditFixture::HasChangedOverlap(*LeftChunk, *ZeroChunk, *LeftBase, *ZeroBase);
		}
		TSharedPtr<const FVoxelLandM5FieldChangeAudit, ESPMode::ThreadSafe> Prior = Audit;
		FVoxelLandM5LatticePoint BoundarySample;
		BoundarySample.X = int64(Coverage.MinChunk.X) * 32;
		Result = FVoxelLandM5FieldChangeAudit::TestCheckSample(BoundarySample, *Influence, Snapshot->Base.VoxelSizeCm,
			EvaluationBudget.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel);
		bMissingOwner = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain
					 && Result.Message == TEXT("Changed sample requires an uncovered edge owner") && Audit == Prior;
		Result = FVoxelLandM5FieldChangeAudit::Build(Field.ToSharedRef(), *Worker, AuditBudget.ToSharedRef(),
			FVoxelLandM5FieldAuditFixture::AlwaysCancel, Audit);
		bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Audit == Prior;
		Result = FVoxelLandM5FieldChangeAudit::Build(
			Field.ToSharedRef(), *Worker, Tiny.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Audit);
		bExhausted = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Audit == Prior;
		Result = FVoxelLandM5FieldChangeAudit::Build(
			Field.ToSharedRef(), *Worker, Source.ToSharedRef(), FVoxelLandM5FieldAuditFixture::NeverCancel, Audit);
		bSourceBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Audit == Prior;
		Async(EAsyncExecution::Thread,
			FVoxelLandM5FieldAuditWrongThreadTask{Field, *Worker, AuditBudget, Prior, bWrongThread})
			.Wait();
		int64 Held = 0;
		int64 Peak = 0;
		int64 Work = 0;
		int64 Released = 0;
		int64 PeakAfter = 0;
		int64 WorkAfter = 0;
		AuditBudget->GetUsage(Held, Peak, Work);
		Audit.Reset();
		Prior.Reset();
		AuditBudget->GetUsage(Released, PeakAfter, WorkAfter);
		bLease = Held > 0 && Released == 0 && WorkAfter == Work;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FieldChangeAuditTest, "VoxelLand.M5.FieldChangeAudit.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FieldChangeAuditTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, FVoxelLandM5FieldAuditControlTask{Control, Error, bControl}).Wait();
	if (!TestTrue(FString::Printf(TEXT("control: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	FVoxelLandM5FieldAuditFixture::ApplyControl(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5FieldAuditFixture::ApplyControl(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5FieldAuditFixture::ApplyControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
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
	Asset->Common.Caves.Limits.MaxValidationWork = 1000000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	Asset->Finite.MinZCm = -6400;
	Asset->Finite.MaxZCm = 12801;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Field change audit fixture requires M4 process budget 256 MiB"));
		return false;
	}
	bool bAll = true;
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		Asset->Common.Caves.Protection.bProtectAll = Mode == 1;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Source;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> AuditBudget;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EvaluationBudget;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Source)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, AuditBudget)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, EvaluationBudget)
			|| !FVoxelLandM5Budget::TryCreate(1, 1, Tiny))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const FVoxelLandM5Diagnostic Captured =
			FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Source.ToSharedRef(), Capture);
		if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message),
				Captured.Code == EVoxelLandM5ErrorCode::None && Capture))
		{
			return false;
		}
		bool bSuccess = false;
		bool bCounts = false;
		bool bSameSign = false;
		bool bHalo = false;
		bool bCancelled = false;
		bool bExhausted = false;
		bool bWrongThread = false;
		bool bSourceBudget = false;
		bool bMissingOwner = false;
		bool bLease = false;
		FString Failure;
		Async(EAsyncExecution::ThreadPool,
			FVoxelLandM5FieldAuditWorkerTask{Capture, Source, AuditBudget, EvaluationBudget, Tiny, Mode == 1, bSuccess,
				bCounts, bSameSign, bHalo, bCancelled, bExhausted, bWrongThread, bSourceBudget, bMissingOwner, bLease,
				Failure})
			.Wait();
		TestTrue(FString::Printf(TEXT("mode %d real audit: %s"), Mode, *Failure), bSuccess);
		TestTrue(TEXT("counts match independent baseline and final evaluation"), bCounts);
		TestTrue(TEXT("same-sign changed density is counted"), bSameSign);
		TestTrue(TEXT("changed halo agrees with adjacent owner"), bHalo);
		TestTrue(TEXT("cancellation preserves prior audit"), bCancelled);
		TestTrue(TEXT("exhaustion preserves prior audit"), bExhausted);
		TestTrue(TEXT("wrong worker thread preserves prior audit"), bWrongThread);
		TestTrue(TEXT("source budget is not a job budget"), bSourceBudget);
		TestTrue(TEXT("boundary sample with missing edge owner rejects"), bMissingOwner);
		TestTrue(TEXT("audit holds and releases its job lease"), bLease);
		bAll &= bSuccess && bCounts && bSameSign && bHalo && bCancelled && bExhausted && bWrongThread && bSourceBudget
			 && bMissingOwner && bLease;
	}
	return bAll;
}
#endif
