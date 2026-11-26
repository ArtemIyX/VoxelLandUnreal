#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Field/VoxelLandCaveBoundBaseline.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Planning/VoxelLandCaveDefinitionInputs.h"
#include "Generation/Caves/Planning/VoxelLandCaveEntranceMask.h"
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

// Guarded test-only candidate seam: the captured descriptor and admitted budget
// remain real; only the opening proposal is controlled. This is not a certificate.
class FVoxelLandM5GatedEntranceFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(const FVoxelLandM5CavePlan& Plan,
		const FVoxelLandM5CaveSettings& Settings, int64 Seed,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(16384, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Copy = Plan;
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, Seed, Plan.Region, MoveTemp(Copy), MoveTemp(Lease)));
	}
};
namespace
{
bool MakeGatedControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
void ApplyGatedControl(
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
FVoxelLandM5PointCm GatedPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5GatedEntranceMaskTest, "VoxelLand.M5.GatedEntranceMask.CapturedBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5GatedEntranceMaskTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeGatedControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	ApplyGatedControl(*Config.Get(), Config->Continental, Control);
	ApplyGatedControl(*Config.Get(), Config->Mountain, Control);
	ApplyGatedControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorldSource> Asset(NewObject<UVoxelLandFiniteWorldSource>());
	Asset->Common.Continental = Config->Continental;
	Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 42;
	Asset->Common.Caves.bEnabled = true;
	Asset->Common.Caves.StageId = FGuid(1, 2, 3, 4);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Topology.MinChambers = Asset->Common.Caves.Topology.MaxChambers = 1;
	Asset->Common.Caves.Topology.EntranceCount = 1;
	Asset->Common.Caves.Topology.MinLoops = Asset->Common.Caves.Topology.MaxLoops = 0;
	Asset->Common.Caves.Limits.MaxValidationWork = 100000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	Asset->Finite.MinZCm = -12800;
	Asset->Finite.MaxZCm = 12801;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Gated entrance fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget, JobBudget, ForeignBudget;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, SourceBudget)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, JobBudget)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, ForeignBudget))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), SourceBudget.ToSharedRef(), Capture);
	if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message),
			Captured.Code == EVoxelLandM5ErrorCode::None && Capture))
	{
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
	bool bSource = false, bInside = false, bStraddle = false, bOutside = false, bProtection = false, bBoundary = false;
	bool bLegacy = false, bForeign = false, bCancelled = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
		auto Registry =
			MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5Bounds Gate;
		Gate.MinInclusive = GatedPoint(2800, -400, -400);
		Gate.MaxExclusive = GatedPoint(3601, 401, 401);
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
		auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, SourceBudget.ToSharedRef(), Never, Definition);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
		Result = FVoxelLandM5InfluenceSet::BuildFinite(
			Definition.ToSharedRef(), SourceBudget.ToSharedRef(), Never, Influence);
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
		FVoxelLandM5CaveSettings Settings = Baseline->GetDefinition().GetSnapshot().Caves;
		Settings.Bounds = Influence->GetDeclaredBounds();
		FVoxelLandM5CavePlan Plan;
		Plan.Bounds = Settings.Bounds;
		FVoxelLandM5Chamber Chamber;
		Chamber.bHasId = true;
		Chamber.Id = 3;
		Plan.Chambers.Add(Chamber);
		FVoxelLandM5Entrance Entrance;
		Entrance.bHasId = Entrance.bHasConnectionId = true;
		Entrance.Id = 4;
		Entrance.ConnectionId = 5;
		Entrance.OpeningBoundsCm = Settings.Bounds;
		Plan.Entrances.Add(Entrance);
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true;
		Connection.Id = 5;
		Connection.Start.bValid = Connection.End.bValid = true;
		Connection.Start.Kind = EVoxelLandM5EndpointKind::Chamber;
		Connection.Start.Id = 3;
		Connection.End.Kind = EVoxelLandM5EndpointKind::Entrance;
		Connection.End.Id = 4;
		Plan.Connections.Add(Connection);
		TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
		Plans.Add(FVoxelLandM5GatedEntranceFixtureFactory::Make(
			Plan, Settings, Snapshot->Base.Seed, SourceBudget.ToSharedRef()));
		const FVoxelLandM5RegionCoord Region;
		bSource = Plans[0] && Plans[0]->UsesBudget(*SourceBudget)
			   && Plans[0]->MatchesSource(Settings, Snapshot->Base.Seed, Region)
			   && &Baseline->GetDefinition() == Definition.Get() && &Influence->GetDefinition() == Definition.Get();
		if (!bSource)
		{
			return;
		}
		const FIntVector Size(17, 9, 9), Start(2, 4, 4), Count(11, 1, 1);
		const auto Origin = GatedPoint(2700, -400, -400);
		const TArray<uint8> Protection = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Mask;
		Result = FVoxelLandM5EntranceMask::BuildGatedForJob(Size, Origin, 100, Start, Count, Protection, Settings,
			Snapshot->Base.Seed, Plans, SourceBudget.ToSharedRef(), JobBudget.ToSharedRef(), Never, Mask);
		Failure = Result.Message;
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Mask || Mask->GetAuthorized().Num() != 11)
		{
			return;
		}
		const auto Values = Mask->GetAuthorized();
		// x=3000 and x=3400 fit the closed ±200 cm envelope exactly;
		// x=2900 and x=3500 straddle the lower and upper gate edges.
		bInside = Values[1] == 1 && Values[2] == 1 && Values[4] == 1 && Values[5] == 1;
		bStraddle = Values[0] == 0 && Values[6] == 0;
		bOutside = Values[10] == 0; // x=3900, envelope [3700,4100] misses the gate.
		bProtection = Values[3] == 0;
		const auto Previous = Mask;
		Result = FVoxelLandM5EntranceMask::BuildForJob(Size, Origin, 100, Start, Count, Protection, Settings,
			Snapshot->Base.Seed, Plans, SourceBudget.ToSharedRef(), JobBudget.ToSharedRef(), Never, Mask);
		bLegacy = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Mask == Previous;
		Result = FVoxelLandM5EntranceMask::BuildGatedForJob(Size, Origin, 100, Start, Count, Protection, Settings,
			Snapshot->Base.Seed, Plans, ForeignBudget.ToSharedRef(), JobBudget.ToSharedRef(), Never, Mask);
		bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Mask == Previous;
		Result = FVoxelLandM5EntranceMask::BuildGatedForJob(
			Size, Origin, 100, Start, Count, Protection, Settings, Snapshot->Base.Seed, Plans,
			SourceBudget.ToSharedRef(), JobBudget.ToSharedRef(), [] { return true; }, Mask);
		bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Mask == Previous;
		// The same x=3200 point, with full halo, from either side of the chunk seam.
		TArray<uint8> Unprotected = {0};
		TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Left, Right;
		const auto L = FVoxelLandM5EntranceMask::BuildGatedForJob(FIntVector(43, 9, 9), GatedPoint(-500, -400, -400),
			100, FIntVector(37, 4, 4), FIntVector(1, 1, 1), Unprotected, Settings, Snapshot->Base.Seed, Plans,
			SourceBudget.ToSharedRef(), JobBudget.ToSharedRef(), Never, Left);
		const auto R = FVoxelLandM5EntranceMask::BuildGatedForJob(FIntVector(43, 9, 9), GatedPoint(2700, -400, -400),
			100, FIntVector(5, 4, 4), FIntVector(1, 1, 1), Unprotected, Settings, Snapshot->Base.Seed, Plans,
			SourceBudget.ToSharedRef(), JobBudget.ToSharedRef(), Never, Right);
		bBoundary = L.Code == EVoxelLandM5ErrorCode::None && R.Code == EVoxelLandM5ErrorCode::None && Left && Right
				 && Left->GetAuthorized().Num() == 1 && Right->GetAuthorized().Num() == 1
				 && Left->GetAuthorized()[0] == 1 && Right->GetAuthorized()[0] == 1;
	}).Wait();
	TestTrue(TEXT("proposal retains exact captured source and budget"), bSource);
	TestTrue(FString::Printf(TEXT("full envelopes inside opening and gate: %s"), *Failure), bInside);
	TestTrue(TEXT("straddling envelopes are unauthorized"), bStraddle);
	TestTrue(TEXT("envelope outside gate is unauthorized"), bOutside);
	TestTrue(TEXT("protection overrides opening"), bProtection);
	TestTrue(TEXT("boundary sample agrees from adjacent chunk windows"), bBoundary);
	TestTrue(TEXT("ungated compatibility call rejects missing gate halo atomically"), bLegacy);
	TestTrue(TEXT("foreign source budget rejects atomically"), bForeign);
	TestTrue(TEXT("cancellation preserves prior mask"), bCancelled);
	return bSource && bInside && bStraddle && bOutside && bProtection && bBoundary && bLegacy && bForeign && bCancelled;
}
#endif
