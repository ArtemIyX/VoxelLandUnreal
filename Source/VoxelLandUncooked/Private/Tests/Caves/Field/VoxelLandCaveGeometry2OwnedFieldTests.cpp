#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "Data/Caves/Field/VoxelLandCaveFinalFieldInputs.h"
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
#include "Generation/Caves/Field/VoxelLandCaveFinalField.h"
#include "Generation/Caves/Geometry/VoxelLandCaveProfileGeometry.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Validation/VoxelLandCaveDefinitionValidator.h"

namespace
{
bool MakeGeometry2Control(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
void ApplyGeometry2Control(
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5Geometry2OwnedFieldVectorsTest,
	"VoxelLand.M5.Geometry2OwnedField.ProfileVectorsAndBorrowedRejection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5Geometry2OwnedFieldVectorsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ProfileGeometry;
	FChamber Chamber;
	Chamber.CenterCm = {};
	Chamber.RadiusCm = 100;
	Chamber.FloorCm = -50;
	Chamber.CeilingCm = 150;
	FVoxelLandM5PointCm Point;
	int64 Distance = 0;
	bool bVectors = TryChamberDistanceQ8(Chamber, Point, Distance) && Distance == -50 * 256;
	Point.X = 160;
	bVectors &= TryChamberDistanceQ8(Chamber, Point, Distance) && Distance == 60 * 256;
	Point.X = 0;
	Point.Z = 180;
	bVectors &= TryChamberDistanceQ8(Chamber, Point, Distance) && Distance == 30 * 256;
	FSegment Segment;
	Segment.StartCm.X = 0;
	Segment.EndCm.X = 200;
	Segment.RadiusCm = 100;
	Segment.StartFloorCm = -50;
	Segment.EndFloorCm = 50;
	Segment.StartCeilingCm = 150;
	Segment.EndCeilingCm = 250;
	Point.X = 100;
	Point.Y = 0;
	Point.Z = 0;
	bVectors &= TrySegmentDistanceQ8(Segment, Point, Distance) && Distance == 0;
	Point.Z = 201;
	bVectors &= TrySegmentDistanceQ8(Segment, Point, Distance) && Distance == 256;
	TestTrue(TEXT("independent profile plane and lateral vectors"), bVectors);

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget))
	{
		return false;
	}
	const TArray<int16> Baseline = {-3000};
	const TArray<uint8> Zero = {0}, One = {1};
	auto Plan = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	FVoxelLandM5Chamber Legacy;
	Legacy.bHasId = true;
	Legacy.RadiusCm = 100;
	Plan->Chambers.Add(Legacy);
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	Plans.Add(Plan);
	FVoxelLandM5FinalFieldInputs Inputs;
	Inputs.Size = FIntVector(1, 1, 1);
	Inputs.SpacingCm = Inputs.VoxelSizeCm = 100;
	Inputs.Baseline = Baseline;
	Inputs.bNetworkEnabled = true;
	Inputs.Plans = Plans;
	Inputs.NetworkAllowed = One;
	Inputs.Protected = Zero;
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Field;
	auto Result = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Field);
	const bool bLegacy = Result.Code == EVoxelLandM5ErrorCode::None && Field && Field->GetDensity().Num() == 1
					  && Field->GetDensity()[0] == 1024;
	const auto Prior = Field;
	Plan->GeometryVersion = 2;
	Plan->Chambers[0].Profile.Version = 2;
	Plan->Chambers[0].Profile.FloorCm = -50;
	Plan->Chambers[0].Profile.CeilingCm = 150;
	Inputs.GeometryVersion = 2;
	Inputs.NetworkAllowed = Zero;
	Result = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Field);
	const bool bRejected = Result.Code == EVoxelLandM5ErrorCode::UnsupportedVersion && Field == Prior;
	Inputs.GeometryVersion = 1;
	Plan->GeometryVersion = 1;
	Result = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Field);
	const bool bMixed = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Field == Prior;
	TestTrue(TEXT("legacy borrowed field retains sphere result"), bLegacy);
	TestTrue(TEXT("borrowed version 2 rejects even with all-zero network mask"), bRejected);
	TestTrue(TEXT("foreign profile version rejects with zero network mask"), bMixed);
	return bVectors && bLegacy && bRejected && bMixed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5Geometry2OwnedFieldRealTest, "VoxelLand.M5.Geometry2OwnedField.RealNative",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5Geometry2OwnedFieldRealTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeGeometry2Control(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	ApplyGeometry2Control(*Config.Get(), Config->Continental, Control);
	ApplyGeometry2Control(*Config.Get(), Config->Mountain, Control);
	ApplyGeometry2Control(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental;
	Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345;
	Asset->Common.BaseHeightQ8 = 2050 * 256;
	Asset->Common.Caves.bEnabled = true;
	Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Topology.MinChambers = Asset->Common.Caves.Topology.MaxChambers = 2;
	Asset->Common.Caves.Topology.MinChamberRadiusCm = Asset->Common.Caves.Topology.MaxChamberRadiusCm = 100;
	Asset->Common.Caves.Topology.MinTunnelRadiusCm = Asset->Common.Caves.Topology.MaxTunnelRadiusCm = 100;
	Asset->Common.Caves.Topology.MinLoops = Asset->Common.Caves.Topology.MaxLoops = 0;
	Asset->Common.Caves.Topology.EntranceCount = 0;
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
		AddError(TEXT("Geometry 2 fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Source, Job, SeamJob, Foreign, Tiny;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Source)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Job)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, SeamJob)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Foreign)
		|| !FVoxelLandM5Budget::TryCreate(1, 1, Tiny))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> NativeSource;
	const auto Captured =
		FVoxelLandM5BoundBase::CaptureFiniteNative(*Asset.Get(), Source.ToSharedRef(), 2, NativeSource);
	if (!TestTrue(FString::Printf(TEXT("source capture: %s"), *Captured.Message),
			Captured.Code == EVoxelLandM5ErrorCode::None && NativeSource))
	{
		return false;
	}
	auto Registry =
		MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
	FVoxelLandM5Bounds Gate;
	Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -500;
	Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 500;
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
	TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
	TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Result = FVoxelLandM5BoundBase::ValidateFiniteNative(
			NativeSource.ToSharedRef(), Registry, Gate, true, true, Never, Definition);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition)
		{
			Failure = Result.Message;
			return;
		}
		Result =
			FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Source.ToSharedRef(), Never, Influence);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence)
		{
			Failure = Result.Message;
		}
	}).Wait();
	if (!TestTrue(FString::Printf(TEXT("v2 native definition: %s"), *Failure), Definition && Influence))
	{
		return false;
	}
	bool bPlan = false, bChanged = false, bOracle = true, bGate = true, bPositiveOutside = false;
	bool bSeam = true, bForeign = false, bBudget = false, bCancel = false, bWork = false;
	int32 OracleCount = 0;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
		auto Result = FVoxelLandM5BoundBase::BuildFinite(
			NativeSource.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
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
			Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(), *Worker, 100000, Never, Seal);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Seal)
		{
			Failure = Result.Message;
			return;
		}
		const auto& Plan = Seal->GetCandidate().GetPlan();
		bPlan = Plan.GeometryVersion == 2 && Plan.Chambers.Num() == 2 && Plan.Segments.Num() == 1;
		for (const auto& C : Plan.Chambers)
		{
			bPlan &= C.Profile.Version == 2 && C.Profile.FloorCm < C.Profile.CeilingCm;
		}
		for (const auto& S : Plan.Segments)
		{
			bPlan &= S.Profile.Version == 2 && S.Profile.StartFloorCm < S.Profile.StartCeilingCm
				  && S.Profile.EndFloorCm < S.Profile.EndCeilingCm;
		}
		TSharedPtr<const FVoxelLandM5FieldInputs, ESPMode::ThreadSafe> Inputs;
		Result = FVoxelLandM5FieldInputs::BindSealed(
			Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Inputs)
		{
			Failure = Result.Message;
			return;
		}
		const auto PriorInputs = Inputs;
		Result = FVoxelLandM5FieldInputs::BindSealed(
			Baseline.ToSharedRef(), Influence.ToSharedRef(), Foreign.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
		bBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Inputs == PriorInputs;
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Other;
		if (FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Source.ToSharedRef(), Never, Other).Code
				== EVoxelLandM5ErrorCode::None
			&& Other)
		{
			Result = FVoxelLandM5FieldInputs::BindSealed(
				Baseline.ToSharedRef(), Other.ToSharedRef(), Source.ToSharedRef(), Seal.ToSharedRef(), Never, Inputs);
			bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Inputs == PriorInputs;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector::ZeroValue;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Ordinary, Output;
		if (Baseline->GetGenerator().GenerateChunkCancellable(Key, *Worker, Ordinary, Never) || !Ordinary)
		{
			return;
		}
		Output = Ordinary;
		Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Job.ToSharedRef(), Never, Output);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Output)
		{
			Failure = Result.Message;
			return;
		}
		for (int32 Z = 0; Z < 35; ++Z)
		{
			for (int32 Y = 0; Y < 35; ++Y)
			{
				for (int32 X = 0; X < 35; ++X)
				{
					const int32 I = X + 35 * (Y + 35 * Z);
					const int32 CX = (X - 1) * 100, CY = (Y - 1) * 100, CZ = (Z - 1) * 100;
					const bool bDifferent = Output->Density[I] != Ordinary->Density[I];
					bChanged |= bDifferent;
					const bool bInside = CX >= -500 && CX < 500 && CY >= -500 && CY < 500 && CZ >= -500 && CZ < 500;
					if (bInside)
					{
						++OracleCount;
						FVoxelLandM5PointCm P;
						P.X = CX;
						P.Y = CY;
						P.Z = CZ;
						int64 MinDistance = MAX_int64, D = 0;
						for (const auto& C : Plan.Chambers)
						{
							VoxelLand::M5::ProfileGeometry::FChamber Profile;
							Profile.CenterCm = C.CenterCm;
							Profile.RadiusCm = C.RadiusCm;
							Profile.FloorCm = C.Profile.FloorCm;
							Profile.CeilingCm = C.Profile.CeilingCm;
							bOracle &= VoxelLand::M5::ProfileGeometry::TryChamberDistanceQ8(Profile, P, D);
							MinDistance = FMath::Min(MinDistance, D);
						}
						for (const auto& S : Plan.Segments)
						{
							VoxelLand::M5::ProfileGeometry::FSegment Profile;
							Profile.StartCm = S.StartCm;
							Profile.EndCm = S.EndCm;
							Profile.RadiusCm = S.RadiusCm;
							Profile.StartFloorCm = S.Profile.StartFloorCm;
							Profile.EndFloorCm = S.Profile.EndFloorCm;
							Profile.StartCeilingCm = S.Profile.StartCeilingCm;
							Profile.EndCeilingCm = S.Profile.EndCeilingCm;
							bOracle &= VoxelLand::M5::ProfileGeometry::TrySegmentDistanceQ8(Profile, P, D);
							MinDistance = FMath::Min(MinDistance, D);
						}
						const int64 Numerator = -MinDistance * 4;
						const int64 Rounded = Numerator >= 0 ? (Numerator + 50) / 100 : -((-Numerator + 50) / 100);
						const int16 Expected = int16(FMath::Clamp<int64>(Rounded, MIN_int16, MAX_int16));
						bOracle &= Output->Density[I] == FMath::Max(Ordinary->Density[I], Expected);
					}
					if (!bInside)
					{
						bGate &= Output->Density[I] == Ordinary->Density[I];
						bPositiveOutside |= Ordinary->Density[I] > 0 && Output->Density[I] == Ordinary->Density[I];
					}
				}
			}
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
					bSeam &= Adjacent->Density[(X + 32) + 35 * (Y + 35 * Z)] == Output->Density[X + 35 * (Y + 35 * Z)];
				}
			}
		}
		const auto Prior = Output;
		Result = VoxelLand::M5::FinalField::EvaluateChunk(*Inputs, Key, *Worker, Tiny.ToSharedRef(), Never, Output);
		bWork = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Output == Prior;
		Result = VoxelLand::M5::FinalField::EvaluateChunk(
			*Inputs, Key, *Worker, Job.ToSharedRef(), [] { return true; }, Output);
		bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Output == Prior;
	}).Wait();
	TestTrue(FString::Printf(TEXT("real owned v2 plan: %s"), *Failure), bPlan);
	TestTrue(TEXT("v2 field contributes inside gate"), bChanged);
	TestTrue(TEXT("all eligible samples match profile distance and Q8 conversion"), bOracle && OracleCount > 0);
	TestTrue(TEXT("positive baseline outside gate stays unchanged"), bPositiveOutside);
	TestTrue(TEXT("full half-open gate preserves outside baseline"), bGate);
	TestTrue(TEXT("negative chunk seam"), bSeam);
	TestTrue(TEXT("foreign influence rejected"), bForeign);
	TestTrue(TEXT("foreign source budget rejected"), bBudget);
	TestTrue(TEXT("job work exhaustion preserves output"), bWork);
	TestTrue(TEXT("cancellation preserves output"), bCancel);
	return bPlan && bChanged && bOracle && OracleCount > 0 && bPositiveOutside && bGate && bSeam && bForeign && bBudget
		&& bWork && bCancel;
}
#endif
