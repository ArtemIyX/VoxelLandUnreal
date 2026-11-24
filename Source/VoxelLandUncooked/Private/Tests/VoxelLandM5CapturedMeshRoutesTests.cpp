#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchMesh.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandM5BoundBase.h"
#include "World/VoxelLandM5CapturedBaseline.h"
#include "World/VoxelLandM5CapturedFiniteCandidate.h"
#include "World/VoxelLandM5CapturedFiniteScratch.h"
#include "World/VoxelLandM5CapturedScratchChunks.h"
#include "World/VoxelLandM5CapturedMeshRoutes.h"
class FVoxelLandM5CapturedRoutesFixture final
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
		if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error)
			|| FVoxelLandProgram::Validate(Draft, Program) || !Program
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
	static void Apply(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target,
		const FVoxelLandM4ControlSnapshot& Source)
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
// Controlled plan only; baseline, capture, settings and budget remain real.
class FVoxelLandM5CapturedMeshRoutesFixture final
{
public:
	static TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Make(
		TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Baseline,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		const FVoxelLandM5WorldSnapshot& Snapshot = Baseline->GetCaptured().GetCapture().GetSnapshot();
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> PlanLease, WrapperLease;
		if (!Budget->TryReserveBytes(65536, PlanLease) || !Budget->TryReserveBytes(4096, WrapperLease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Plan;
		Plan.Bounds = Snapshot.Caves.Bounds;
		FVoxelLandM5Chamber A;
		A.bHasId = true;
		A.Id = 1;
		A.CenterCm.X = 475;
		A.CenterCm.Y = 530;
		A.CenterCm.Z = 1150;
		A.RadiusCm = 100;
		FVoxelLandM5Chamber B = A;
		B.Id = 2;
		B.CenterCm.X = 675;
		Plan.Chambers = {A, B};
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true;
		Connection.Id = 7;
		Connection.Start.bValid = Connection.End.bValid = true;
		Connection.Start.Id = 1;
		Connection.End.Id = 2;
		Plan.Connections.Add(Connection);
		FVoxelLandM5Segment Segment;
		Segment.bHasId = Segment.bHasConnectionId = true;
		Segment.Id = 9;
		Segment.ConnectionId = 7;
		Segment.StartCm = A.CenterCm;
		Segment.EndCm = B.CenterCm;
		Segment.RadiusCm = 100;
		Plan.Segments.Add(Segment);
		const FVoxelLandM5RegionCoord Region;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate =
			MakeShareable(new FVoxelLandM5CandidateResult(Budget, Snapshot.Caves, Snapshot.Base.Seed,
				Region, MoveTemp(Plan), MoveTemp(PlanLease)));
		TSharedPtr<FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Result =
			MakeShareable(new FVoxelLandM5CapturedFiniteCandidate(Baseline, MoveTemp(WrapperLease)));
		Result->Candidate = MoveTemp(Candidate);
		return Result;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CapturedMeshRoutesTest, "VoxelLand.M5.CapturedMeshRoutes.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5CapturedMeshRoutesTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture fixture runs on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = FVoxelLandM5CapturedRoutesFixture::MakeControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Definition(NewObject<UVoxelLandFiniteWorld>());
	Definition->Common.Continental = Config->Continental;
	Definition->Common.Mountain = Config->Mountain;
	Definition->Common.Peaks = Config->Peaks;
	Definition->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	// Reuse the independent pocket vector at (0,0,0): seed -1, this
	// stage/salt and period 16 produce signed sample 24801.
	Definition->Common.Seed = -1;
	Definition->Common.BaseHeightQ8 = 1050 * 256;
	Definition->Common.Caves.StageId = FGuid(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	Definition->Common.Caves.RoofThicknessCm = 0;
	Definition->Common.Caves.Protection.bProtectAll = true;
	Definition->Common.Caves.Topology.MinChamberRadiusCm = 100;
	Definition->Common.Caves.Topology.MaxChamberRadiusCm = 100;
	Definition->Common.Caves.Topology.MinTunnelRadiusCm = 100;
	Definition->Common.Caves.Topology.MaxTunnelRadiusCm = 100;
	Definition->Common.Caves.Capsule.ClearanceMarginCm = 0;
	Definition->Common.Caves.Pockets.LatticeSpacingCm = 16;
	Definition->Common.Caves.Pockets.Salt = 0x0102030405060708ll;
	Definition->Common.Caves.Pockets.ThresholdQ16 = 0;
	Definition->Common.Caves.Pockets.MinDepthCm = 0;
	Definition->Common.Caves.Pockets.MaxDepthCm = 10000;
	Definition->Common.Caves.Topology.MinChambers = 2;
	Definition->Common.Caves.Topology.MaxChambers = 2;
	Definition->Common.Caves.Topology.MinLoops = 0;
	Definition->Common.Caves.Topology.MaxLoops = 0;
	Definition->Common.Caves.Topology.EntranceCount = 0;
	Definition->Common.Caves.Limits.MaxValidationWork = 1000000000;
	Definition->Finite.bGlobalErosion = false;

	Definition->Finite.CellsX = Definition->Finite.CellsY = 4;
	Definition->Finite.OriginXCm = Definition->Finite.OriginYCm = -12800;
	Definition->Finite.GridSpacingCm = 6400;
	Definition->Finite.MinZCm = -12800;
	// MaxZCm is already exclusive; X/Y conversion includes their last grid point.
	Definition->Finite.MaxZCm = 12801;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 budget cvar"), ProcessBudget))
	{
		return false;
	}
	if (ProcessBudget->GetInt() != 256)
	{
		AddWarning(TEXT("Captured chunks fixture skipped: M4 budget is not 256 MiB"));
		return true;
	}
	const auto Factory = [](const FVoxelLandGenerator& Generator,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	};
	const auto Never = [] { return false; };
	bool bNetwork = false, bPockets = false, bHalo = false, bWrongMode = false, bCancelled = false, bLifetime = false;
	bool bMesh = false, bMismatch = false, bBounds = false, bRouteCancelled = false, bConnectedRoute = false;
	for (int32 Mode = 0; Mode < 1; ++Mode)
	{
		Definition->Common.Caves.bEnabled = Mode == 0;
		Definition->Common.Caves.Pockets.bEnabled = Mode == 1;
		Definition->Common.Caves.Pockets.AmplitudeCm = Mode == 1 ? 1000 : 0;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const FVoxelLandM5Diagnostic Admission = FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
		if (Admission.Code == EVoxelLandM5ErrorCode::BudgetExceeded
			&& Admission.Message == TEXT("M4 preparation reservation unavailable"))
		{
			AddWarning(TEXT("Captured chunks fixture skipped: M4 process admission unavailable"));
			return true;
		}
		if (!TestTrue(FString::Printf(TEXT("captured source: %s"), *Admission.Message),
			Admission.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
		{
			return false;
		}
		FString Failure;
		Async(EAsyncExecution::ThreadPool, [&] {
			TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
			FVoxelLandM5Diagnostic Result = FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Scratch)
			{
				Failure = Result.Message;
				return;
			}
			// Three chunks per axis include the extra carve-mask halo for chunk zero.
			TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source;
			Result = FVoxelLandM5CapturedBaseline::Build(Scratch.ToSharedRef(),
				FIntVector(-1, -1, -1), FIntVector(2, 2, 2), 27, Never, Source);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Source)
			{
				Failure = Result.Message;
				return;
			}
			TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate;
			if (Mode == 0)
			{
				Candidate = FVoxelLandM5CapturedMeshRoutesFixture::Make(Source.ToSharedRef(), Budget.ToSharedRef());
				if (!Candidate)
				{
					Failure = TEXT("Controlled connected candidate admission failed");
					return;
				}
			}
			TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Chunks;
			if (Mode == 0)
			{
				Result = FVoxelLandM5CapturedScratchChunks::BuildNetwork(Candidate.ToSharedRef(),
					FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, Chunks);
			}
			else
			{
				Result = FVoxelLandM5CapturedScratchChunks::BuildPocketOnly(Source.ToSharedRef(),
					FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, Chunks);
			}
			Failure = Result.Message;
			const FVoxelLandM5WorldSnapshot& Snapshot = Capture->GetSnapshot();
			const bool bMatches = Result.Code == EVoxelLandM5ErrorCode::None && Chunks
				&& &Chunks->GetSource() == Source.Get() && Chunks->GetCandidate() == Candidate.Get()
				&& Chunks->GetChunks().IsOwnedBy(*Budget) && Chunks->GetChunks().GetChunks().Num() == 1
				&& Chunks->GetChunks().MatchesDependencies(Source->GetWindow(),
					Candidate ? &Candidate->GetCandidate() : nullptr, nullptr, Snapshot.Base.Seed, Snapshot.Manifest)
				&& Chunks->GetChunks().MatchesSettings(Snapshot.Caves);
			if (Mode == 0)
			{
				bNetwork = bMatches;
			}
			if (bMatches && Mode == 0 && Candidate)
			{
				TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
				const FVoxelLandM5Diagnostic Meshed = FVoxelLandM5ScratchMesh::Build(
					Chunks->GetChunksShared(), 1, 10000, Budget.ToSharedRef(), Never, Mesh);
				Failure = Meshed.Message;
				bMesh = Meshed.Code == EVoxelLandM5ErrorCode::None && Mesh && Mesh->GetTriangles().Num() > 0;
				if (bMesh)
				{
					TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> Routes;
					const FVoxelLandM5Diagnostic Connected = FVoxelLandM5CapturedMeshRoutes::Build(
						Chunks.ToSharedRef(), Mesh.ToSharedRef(), 200, Never, Routes);
					Failure = Connected.Message;
					bConnectedRoute = Connected.Code == EVoxelLandM5ErrorCode::None && Routes
						&& Routes->GetCandidate().GetPlan().Routes.Num() == 1
						&& Routes->GetCandidate().GetPlan().Routes[0].Id == 7
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs.Num() == 1
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].StartFeetCm.X == 475
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].StartFeetCm.Y == 530
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].StartFeetCm.Z == 1050
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].EndFeetCm.X == 675
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].EndFeetCm.Y == 530
						&& Routes->GetCandidate().GetPlan().Routes[0].Legs[0].EndFeetCm.Z == 1050;
					const TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> PreviousRoutes = Routes;
					const FVoxelLandM5Diagnostic Bounds = FVoxelLandM5CapturedMeshRoutes::Build(
						Chunks.ToSharedRef(), Mesh.ToSharedRef(), 1000, Never, Routes);
					const FVoxelLandM5Diagnostic InvalidDepth = FVoxelLandM5CapturedMeshRoutes::Build(
						Chunks.ToSharedRef(), Mesh.ToSharedRef(), 0, Never, Routes);
					bBounds = Bounds.Code == EVoxelLandM5ErrorCode::OutOfDomain
						&& InvalidDepth.Code == EVoxelLandM5ErrorCode::InvalidInput && Routes == PreviousRoutes;
					const FVoxelLandM5Diagnostic CancelRoute = FVoxelLandM5CapturedMeshRoutes::Build(
						Chunks.ToSharedRef(), Mesh.ToSharedRef(), 200, [] { return true; }, Routes);
					bRouteCancelled = CancelRoute.Code == EVoxelLandM5ErrorCode::Cancelled && Routes == PreviousRoutes;
					TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> OtherChunks;
					const FVoxelLandM5Diagnostic Other = FVoxelLandM5CapturedScratchChunks::BuildNetwork(
						Candidate.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, OtherChunks);
					if (Other.Code == EVoxelLandM5ErrorCode::None && OtherChunks)
					{
						const FVoxelLandM5Diagnostic Mismatch = FVoxelLandM5CapturedMeshRoutes::Build(
							OtherChunks.ToSharedRef(), Mesh.ToSharedRef(), 100, Never, Routes);
						bMismatch = Mismatch.Code == EVoxelLandM5ErrorCode::InvalidInput && Routes == PreviousRoutes;
					}
				}
			}
			const TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Prior = Chunks;
			if (Mode == 0)
			{
				const FVoxelLandM5Diagnostic Wrong = FVoxelLandM5CapturedScratchChunks::BuildPocketOnly(
					Source.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Never, Chunks);
				bWrongMode = Wrong.Code == EVoxelLandM5ErrorCode::InvalidInput && Chunks == Prior;
				const FVoxelLandM5Diagnostic Cancel = FVoxelLandM5CapturedScratchChunks::BuildNetwork(
					Candidate.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, [] { return true; }, Chunks);
				bCancelled = Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Chunks == Prior;
				// The outermost chunk of the baseline lacks the required extra halo.
				const FVoxelLandM5Diagnostic Halo = FVoxelLandM5CapturedScratchChunks::BuildNetwork(
					Candidate.ToSharedRef(), FIntVector(-1, -1, -1), FIntVector(0, 0, 0), 1, Never, Chunks);
				bHalo = Halo.Code != EVoxelLandM5ErrorCode::None && Chunks == Prior;
				TWeakPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> WeakSource = Source;
				Candidate.Reset();
				Source.Reset();
				Scratch.Reset();
				bLifetime = Chunks.IsValid() && WeakSource.IsValid();
			}
		}).Wait();
		TestTrue(FString::Printf(TEXT("captured chunks mode %d: %s"), Mode, *Failure), Mode == 0 ? bNetwork : bPockets);
		Capture.Reset();
	}
	TestTrue(TEXT("actual captured scratch mesh"), bMesh);
	TestTrue(TEXT("foreign scratch mesh rejected"), bMismatch);
	TestTrue(TEXT("invalid search depth rejected"), bBounds);
	TestTrue(TEXT("route cancellation preserves output"), bRouteCancelled);
	TestTrue(TEXT("controlled connected source resolves actual mesh feet"), bConnectedRoute);
	TestTrue(TEXT("network mode rejects pocket entry"), bWrongMode);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("missing halo preserves output"), bHalo);
	TestTrue(TEXT("chunks retain captured source"), bLifetime);
	return bNetwork && bMesh && bMismatch && bBounds && bRouteCancelled && bConnectedRoute;
}
#endif
