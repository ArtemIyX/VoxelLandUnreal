#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CarveMasks.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5PocketSurfaceColumns.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Generation/VoxelLandM5WindowCarveMasks.h"
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

// Controlled extrema only; this seam creates no field certificate or publication authority.
class FVoxelLandM5PocketColumnsExtremaFixture final
{
public:
	static void SetNonuniform(const FVoxelLandM5PocketSurfaceColumns& Source)
	{
		auto& Mutable = const_cast<FVoxelLandM5PocketSurfaceColumns&>(Source);
		for (int32 Y = 0; Y < Mutable.TileSizeY; ++Y) for (int32 X = 0; X < Mutable.TileSizeX; ++X)
		{
			const int32 I = X + Mutable.TileSizeX * Y;
			Mutable.Lower[I] = X == 0 && Y == 0 ? 1000 : 1900;
			Mutable.Upper[I] = X == Mutable.TileSizeX - 1 && Y == Mutable.TileSizeY - 1 ? 3000 : 2200;
		}
	}
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5BaselineWindow& Window,
		const FVoxelLandM5PocketSurfaceColumns& Columns, FIntVector Start, FIntVector Count,
		TConstArrayView<uint8> Protection, TConstArrayView<uint8> Entrance,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& Out)
	{
		return FVoxelLandM5CarveMasks::BuildImpl(Window.GetSize(), Window.GetOriginCm(), Window.GetVoxelSizeCm(),
			Start, Count, Window.GetDensity(), Protection, Entrance, 0, true, 2000, 3500, true,
			Budget, [] { return false; }, Out, &Columns);
	}
};

static_assert(!std::is_copy_constructible_v<FVoxelLandM5PocketSurfaceColumns>);
static_assert(!std::is_copy_assignable_v<FVoxelLandM5PocketSurfaceColumns>);

namespace
{
bool MakePocketColumnControl(FVoxelLandM4ControlSnapshot& Out, FString& Error)
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
	Link.SourceNode = Constant.Id; Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id; Link.TargetPin = Output.Pins[0].Id;
	Out.Source.Nodes = {Constant, Output}; Out.Source.Links = {Link};
	FVoxelLandProgramDraft Draft;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error)
		|| FVoxelLandProgram::Validate(Draft, Program) || !Program
		|| !FVoxelLandProgramCodec::Pack(Out.Source, *Program, Out.Payload, Error)) return false;
	FVoxelLandM4CurveAuthoringKey First, Last; Last.InputQ16 = 65535;
	Out.CurveKeys = {First, Last}; Out.CookedCurve.SourceKeys = Out.CurveKeys;
	Out.CookedCurve.BakeVersion = 1; Out.CookedCurve.SamplesQ8.Init(0, 4097);
	FCanonicalWriter Writer;
	for (int32 Sample : Out.CookedCurve.SamplesQ8) Writer.U32(uint32(Sample));
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	FMemory::Memcpy(&Out.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
	return true;
}
void ApplyPocketColumnControl(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target,
	const FVoxelLandM4ControlSnapshot& Source)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Owner);
	Graph->SourceVersion = Source.Source.SourceVersion; Graph->Parameters = Source.Source.Parameters;
	Graph->Nodes = Source.Source.Nodes; Graph->Links = Source.Source.Links; Graph->CompiledProgram = Source.Payload;
	Target.Graph = Graph; Target.CurveKeys = Source.CurveKeys; Target.CookedCurve = Source.CookedCurve;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PocketSurfaceColumnsTest,
	"VoxelLand.M5.PocketSurfaceColumns.RealFinite", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PocketSurfaceColumnsTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread())) return false;
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakePocketColumnControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl)) return false;
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	ApplyPocketColumnControl(*Config.Get(), Config->Continental, Control);
	ApplyPocketColumnControl(*Config.Get(), Config->Mountain, Control);
	ApplyPocketColumnControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental; Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.BaseHeightQ8 = 2050 * 256;
	Asset->Common.bSmoothEnabled = true; Asset->Common.SmoothStrength = 1.f;
	Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.Pockets.bEnabled = true;
	Asset->Common.Caves.Pockets.AmplitudeCm = 100;
	Asset->Common.Caves.Pockets.MinDepthCm = 2800;
	Asset->Common.Caves.Pockets.MaxDepthCm = 3300;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Limits.MaxValidationWork = 1000000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	Asset->Finite.MinZCm = -250000; Asset->Finite.MaxZCm = 250001;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Pocket column fixture requires the M4 process budget to be 256 MiB")); return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, ForeignBudget;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Budget)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, ForeignBudget)) return false;
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), Capture);
	if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message), Captured.Code == EVoxelLandM5ErrorCode::None && Capture)) return false;
	const auto Never = [] { return false; };
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result; if (Generator.CreateWorker(Worker)) Result.Code = EVoxelLandM5ErrorCode::InvalidInput; return Result;
	};
	bool bSetup = false, bTall = false, bAllNeighbors = false, bMask = false, bForeign = false, bCancel = false, bBounds = false;
	bool bWrongThread = false, bForeignInfluence = false, bMissingColumn = false, bMultiDepth = false, bColumnWork = false, bColumnCancel = false;
	bool bExtremaOracle = false, bExtremaMissing = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
		auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
		Gate.MinInclusive.X = Gate.MinInclusive.Y = -1000; Gate.MaxExclusive.X = Gate.MaxExclusive.Y = 1000;
		Gate.MinInclusive.Z = -10000; Gate.MaxExclusive.Z = 10000;
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
		auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, Definition);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
		Result = FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Budget.ToSharedRef(), Never, Influence);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
		Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(Capture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Baseline) { Failure = Result.Message; return; }
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) { Failure = TEXT("worker creation failed"); return; }
		bSetup = true;
		FVoxelLandM5PointCm Min; Min.X = 0; Min.Y = 0; Min.Z = -10;
		TSharedPtr<const FVoxelLandM5PocketSurfaceColumns, ESPMode::ThreadSafe> Columns;
		Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
			Min, FIntPoint(1, 1), Budget.ToSharedRef(), Never, Columns);
		Failure = Result.Message;
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Columns) return;
		// Smoothing trims one raw halo sample at each end: 4999 post-filter
		// samples. First air at lattice 21 begins slab 72, after solid at 20.
		bTall = Influence->GetCoverage().PocketColumnBoundsCm.MinInclusive.Z == -249900
			&& Influence->GetCoverage().PocketColumnBoundsCm.MaxExclusive.Z == 249901;
		bAllNeighbors = true;
		for (int32 Y = -2; Y <= 2; ++Y) for (int32 X = -2; X <= 2; ++X)
		{
			int64 Low = 0, High = 0;
			bAllNeighbors &= Columns->GetSurfaceBounds(X, Y, Low, High) && Low == 1900 && High == 2200;
		}
		int64 Low = 123, High = 456;
		bBounds = !Columns->GetSurfaceBounds(3, 0, Low, High)
			&& !Columns->GetSurfaceBounds(MIN_int64, 0, Low, High)
			&& !Columns->GetSurfaceBounds(MAX_int64, 0, Low, High)
			&& !Columns->GetSurfaceBounds(0, MIN_int64, Low, High)
			&& !Columns->GetSurfaceBounds(0, MAX_int64, Low, High)
			&& Low == 123 && High == 456;
		const auto Prior = Columns;
		Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
			Min, FIntPoint(1, 1), ForeignBudget.ToSharedRef(), Never, Columns);
		bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Columns == Prior;
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> OtherDefinition;
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> OtherInfluence;
		if (FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, OtherDefinition).Code == EVoxelLandM5ErrorCode::None
			&& OtherDefinition && FVoxelLandM5InfluenceSet::BuildFinite(OtherDefinition.ToSharedRef(), Budget.ToSharedRef(), Never, OtherInfluence).Code == EVoxelLandM5ErrorCode::None && OtherInfluence)
		{
			Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), OtherInfluence.ToSharedRef(), *Worker,
				Min, FIntPoint(1, 1), Budget.ToSharedRef(), Never, Columns);
			bForeignInfluence = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Columns == Prior;
		}
		Async(EAsyncExecution::ThreadPool, [&] {
			auto OtherOutput = Columns;
			const auto WrongThread = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
				Min, FIntPoint(1, 1), Budget.ToSharedRef(), Never, OtherOutput);
			bWrongThread = WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && OtherOutput == Prior;
		}).Wait();
		int32 Polls = 0;
		Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
			Min, FIntPoint(1, 1), Budget.ToSharedRef(), [&] { return ++Polls > 8; }, Columns);
		bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Polls > 8 && Columns == Prior;
		FVoxelLandM5PointCm WindowMin; WindowMin.X = -4; WindowMin.Y = -4; WindowMin.Z = -14;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		Result = FVoxelLandM5BaselineWindow::BuildBox(Baseline->GetGeneratorShared(), *Worker, WindowMin, FIntVector(10, 9, 11), Budget.ToSharedRef(), Never, Window);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Window) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
		Result = VoxelLand::M5::WindowCarveMasks::BuildOwned(*Baseline, *Influence, *Columns, *Window, nullptr,
			FIntVector(4, 4, 4), FIntVector(1, 1, 1), Budget.ToSharedRef(), Never, Masks);
		Failure = Result.Message;
		bMask = Result.Code == EVoxelLandM5ErrorCode::None && Masks && Masks->GetPocketAllowed().Num() == 1
			&& Masks->GetPocketAllowed()[0] == 1 && Masks->GetProtected()[0] == 0;
		int64 Live = 0, Peak = 0, BeforeWork = 0, AfterWork = 0;
		Budget->GetUsage(Live, Peak, BeforeWork);
		TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Multi;
		Result = VoxelLand::M5::WindowCarveMasks::BuildOwned(*Baseline, *Influence, *Columns, *Window, nullptr,
			FIntVector(4, 4, 4), FIntVector(1, 1, 3), Budget.ToSharedRef(), Never, Multi);
		Budget->GetUsage(Live, Peak, AfterWork);
		bMultiDepth = Result.Code == EVoxelLandM5ErrorCode::None && Multi && Multi->GetPocketAllowed().Num() == 3
			&& Multi->GetPocketAllowed()[0] == 1 && Multi->GetPocketAllowed()[1] == 1 && Multi->GetPocketAllowed()[2] == 0;
		// Independent accounting: wrapper 2*990+2*3+1; mask scan and
		// output 3+3*10, one 25-column query (75), roof 8*8*10 cells
		// (20 each), 9*9*11 prefix entries, three 8-read queries, final 1.
		bColumnWork = AfterWork - BeforeWork == (2 * 990 + 2 * 3 + 1)
			+ (3 + 3 * 10 + 75 + 8 * 8 * 10 * 20 + 9 * 9 * 11 + 3 * 8 + 1);
		const auto PriorMulti = Multi;
		const int64 CancelThreshold = AfterWork + (2 * 990 + 2 * 3 + 1) + 3;
		const auto CancelDuringColumns = [&Budget, CancelThreshold] {
			int64 A = 0, B = 0, C = 0; Budget->GetUsage(A, B, C);
			return C > CancelThreshold;
		};
		Result = VoxelLand::M5::WindowCarveMasks::BuildOwned(*Baseline, *Influence, *Columns, *Window, nullptr,
			FIntVector(4, 4, 4), FIntVector(1, 1, 3), Budget.ToSharedRef(), CancelDuringColumns, Multi);
		bColumnCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Multi == PriorMulti;
		const auto PriorMasks = Masks;
		Result = VoxelLand::M5::WindowCarveMasks::BuildOwned(*Baseline, *Influence, *Columns, *Window, nullptr,
			FIntVector(5, 4, 4), FIntVector(1, 1, 1), Budget.ToSharedRef(), Never, Masks);
		bMissingColumn = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Masks == PriorMasks;
		// Build real owner-bound columns, then use a guarded test-only seam to
		// vary their evidence. Extremes at opposite tile corners appear in
		// different 5×5 neighborhoods; one selected sample is protected.
		TSharedPtr<const FVoxelLandM5PocketSurfaceColumns, ESPMode::ThreadSafe> Varying;
		Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
			Min, FIntPoint(2, 2), Budget.ToSharedRef(), Never, Varying);
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> OracleWindow;
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Varying
			|| FVoxelLandM5BaselineWindow::BuildBox(Baseline->GetGeneratorShared(), *Worker, WindowMin,
				FIntVector(11, 10, 11), Budget.ToSharedRef(), Never, OracleWindow).Code != EVoxelLandM5ErrorCode::None
			|| !OracleWindow) return;
		FVoxelLandM5PocketColumnsExtremaFixture::SetNonuniform(*Varying);
		TArray<uint8> Protected, Authorized;
		Protected.Init(0, 11 * 10 * 11); Authorized.Init(0, Protected.Num());
		Protected[5 + 11 * (4 + 10 * 4)] = 1;
		TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> OracleMasks;
		Result = FVoxelLandM5PocketColumnsExtremaFixture::Build(*OracleWindow, *Varying, FIntVector(4, 4, 4),
			FIntVector(2, 2, 3), Protected, Authorized, Budget.ToSharedRef(), OracleMasks);
		bExtremaOracle = Result.Code == EVoxelLandM5ErrorCode::None && OracleMasks
			&& OracleMasks->GetPocketAllowed().Num() == 12;
		if (bExtremaOracle)
		{
			for (int32 Z = 0; Z < 3; ++Z) for (int32 Y = 0; Y < 2; ++Y) for (int32 X = 0; X < 2; ++X)
			{
				int64 Lowest = MAX_int64, Highest = MIN_int64;
				for (int32 DY = -2; DY <= 2; ++DY) for (int32 DX = -2; DX <= 2; ++DX)
				{
					int64 Lo = 0, Hi = 0;
					bExtremaOracle &= Varying->GetSurfaceBounds(X + DX, Y + DY, Lo, Hi);
					Lowest = FMath::Min(Lowest, Lo); Highest = FMath::Max(Highest, Hi);
				}
				const int64 SampleZ = -1000 + 100 * Z;
				const bool bProtected = X == 1 && Y == 0 && Z == 0;
				const uint8 Expected = !bProtected && Lowest - SampleZ >= 2000 && Highest - SampleZ <= 3500;
				bExtremaOracle &= OracleMasks->GetPocketAllowed()[X + 2 * (Y + 2 * Z)] == Expected;
			}
			// Explicit cases detect missing minimum, missing maximum and protection.
			bExtremaOracle &= OracleMasks->GetPocketAllowed()[0] == 1
				&& OracleMasks->GetPocketAllowed()[4] == 0 && OracleMasks->GetPocketAllowed()[3] == 0
				&& OracleMasks->GetPocketAllowed()[1] == 0;
		}
		const auto PriorOracle = OracleMasks;
		Result = FVoxelLandM5PocketColumnsExtremaFixture::Build(*OracleWindow, *Varying, FIntVector(5, 4, 4),
			FIntVector(2, 2, 3), Protected, Authorized, Budget.ToSharedRef(), OracleMasks);
		bExtremaMissing = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && OracleMasks == PriorOracle;
	}).Wait();
	// A second real M4 source puts the surface exactly on lattice Z=20.
	// The zero lies far outside the local window used above and must reject.
	Capture.Reset();
	Asset->Common.BaseHeightQ8 = 2000 * 256;
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> ZeroCapture;
	const auto ZeroAdmission = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), ZeroCapture);
	bool bZeroRejected = false;
	if (ZeroAdmission.Code == EVoxelLandM5ErrorCode::None && ZeroCapture)
	{
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(ZeroCapture->GetSnapshot());
			auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
			FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
			Gate.MinInclusive.X = Gate.MinInclusive.Y = -1000; Gate.MaxExclusive.X = Gate.MaxExclusive.Y = 1000;
			Gate.MinInclusive.Z = -10000; Gate.MaxExclusive.Z = 10000;
			FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			if (FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, Definition).Code != EVoxelLandM5ErrorCode::None || !Definition) return;
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
			if (FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Budget.ToSharedRef(), Never, Influence).Code != EVoxelLandM5ErrorCode::None || !Influence) return;
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
			if (FVoxelLandM5CapturedFiniteScratch::BuildBound(ZeroCapture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline).Code != EVoxelLandM5ErrorCode::None || !Baseline) return;
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) return;
			FVoxelLandM5PointCm Min; Min.Z = -10;
			TSharedPtr<const FVoxelLandM5PocketSurfaceColumns, ESPMode::ThreadSafe> Output;
			const auto Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker,
				Min, FIntPoint(1, 1), Budget.ToSharedRef(), Never, Output);
			bZeroRejected = Result.Code == EVoxelLandM5ErrorCode::Unproven && !Output;
		}).Wait();
	}
	// Native SSE2 vector discovered independently with the vendored FastNoise
	// implementation and M4 integer quantization. Verify the real generator's
	// signs before asking the column scanner to reject the later crossing.
	ZeroCapture.Reset();
	Asset->Common.BaseHeightQ8 = 2050 * 256;
	Asset->Common.bSmoothEnabled = false;
	Asset->Common.Seed = 12345;
	Asset->Common.bOverhangEnabled = true;
	Asset->Common.OverhangAmplitudeQ8 = 128000;
	Asset->Common.OverhangBandQ8 = 1280000;
	Asset->Common.OverhangFrequencyPerCm = 0.004f;
	Asset->Common.OverhangNodeId = FGuid(11, 22, 33, 44);
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> CrossingCapture;
	const auto CrossingAdmission = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), CrossingCapture);
	bool bCrossingSamples = false, bCrossingRejected = false;
	if (CrossingAdmission.Code == EVoxelLandM5ErrorCode::None && CrossingCapture)
	{
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(CrossingCapture->GetSnapshot());
			auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
			FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
			Gate.MinInclusive.X = -9000; Gate.MaxExclusive.X = -4000;
			Gate.MinInclusive.Y = -8500; Gate.MaxExclusive.Y = -6500;
			Gate.MinInclusive.Z = -10000; Gate.MaxExclusive.Z = 10000;
			FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
			TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
			if (FVoxelLandM5DefinitionValidator::Validate(Inputs, Budget.ToSharedRef(), Never, Definition).Code != EVoxelLandM5ErrorCode::None || !Definition) return;
			TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
			if (FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Budget.ToSharedRef(), Never, Influence).Code != EVoxelLandM5ErrorCode::None || !Influence) return;
			TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
			if (FVoxelLandM5CapturedFiniteScratch::BuildBound(CrossingCapture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline).Code != EVoxelLandM5ErrorCode::None || !Baseline) return;
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) return;
			FVoxelLandM5PointCm Probe; Probe.X = -74; Probe.Y = -73; Probe.Z = 18;
			TArray<int16> Actual;
			const auto Sampled = Baseline->GetGenerator().GenerateFiniteM4Box(Probe, FIntVector(1, 1, 2), *Worker, Budget.ToSharedRef(), Never, Actual);
			bCrossingSamples = Sampled.Code == EVoxelLandM5ErrorCode::None && Actual.Num() == 2 && Actual[0] == 36 && Actual[1] == -1010;
			if (!bCrossingSamples) return;
			FVoxelLandM5PointCm Safe; Safe.X = -49; Safe.Y = -75; Safe.Z = -10;
			TSharedPtr<const FVoxelLandM5PocketSurfaceColumns, ESPMode::ThreadSafe> Output;
			if (FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker, Safe,
				FIntPoint(1, 1), Budget.ToSharedRef(), Never, Output).Code != EVoxelLandM5ErrorCode::None || !Output) return;
			const auto Prior = Output;
			FVoxelLandM5PointCm Bad; Bad.X = -75; Bad.Y = -75; Bad.Z = -10;
			const auto Result = FVoxelLandM5PocketSurfaceColumns::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), *Worker, Bad,
				FIntPoint(1, 1), Budget.ToSharedRef(), Never, Output);
			bCrossingRejected = Result.Code == EVoxelLandM5ErrorCode::Unproven
				&& Result.Message == TEXT("Pocket surface is ambiguous or has multiple crossings") && Output == Prior;
		}).Wait();
	}
	TestTrue(TEXT("native M4 crossing vector matches independent SSE2 calculation"), bCrossingSamples);
	TestTrue(TEXT("later solid crossing outside local window rejects and preserves output"), bCrossingRejected);
	TestTrue(TEXT("real zero outside local window rejects complete column"), bZeroRejected);
	TestTrue(FString::Printf(TEXT("bound finite setup: %s"), *Failure), bSetup);
	TestTrue(TEXT("full tall negative-Z domain scanned"), bTall);
	TestTrue(TEXT("all 25 neighboring columns straddle slab seam"), bAllNeighbors);
	TestTrue(TEXT("column lookup rejects missing neighbor"), bBounds);
	TestTrue(TEXT("foreign budget preserves prior columns"), bForeign);
	TestTrue(TEXT("equal but foreign influence preserves prior columns"), bForeignInfluence);
	TestTrue(TEXT("wrong-thread worker preserves prior columns"), bWrongThread);
	TestTrue(TEXT("missing neighboring column preserves prior masks"), bMissingColumn);
	TestTrue(TEXT("mid-scan cancellation preserves prior columns"), bCancel);
	TestTrue(FString::Printf(TEXT("owned mask uses complete column bounds: %s"), *Failure), bMask);
	TestTrue(TEXT("reused extrema preserve per-Z pocket depth boundaries"), bMultiDepth);
	TestTrue(TEXT("owned extrema work is charged once per XY"), bColumnWork);
	TestTrue(TEXT("cancellation during extrema precompute preserves output"), bColumnCancel);
	TestTrue(TEXT("nonuniform 5x5 extrema agree with independent oracle"), bExtremaOracle);
	TestTrue(TEXT("missing nonuniform neighbor preserves output"), bExtremaMissing);
	return bSetup && bTall && bAllNeighbors && bBounds && bForeign && bForeignInfluence && bWrongThread && bMissingColumn && bCancel && bMask && bMultiDepth && bColumnWork && bColumnCancel && bExtremaOracle && bExtremaMissing && bZeroRejected && bCrossingSamples && bCrossingRejected;
}
#endif
