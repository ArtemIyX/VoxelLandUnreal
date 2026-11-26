#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchMesh.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Field/VoxelLandCaveCapturedBaseline.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteCandidate.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"
#include "World/Caves/Field/VoxelLandCaveCapturedScratchChunks.h"
#include "World/Caves/Geometry/VoxelLandCaveCapturedMeshRoutes.h"
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
	static void Apply(
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
};
// Controlled plan only; baseline, capture, settings and budget remain real.
class FVoxelLandM5CapturedMeshRoutesFixture final
{
public:
	// Controlled fixture mutations occur only after previous mesh readers finish.
	static TArray<int16> SetWall(const FVoxelLandM5ScratchChunks& Source)
	{
		TArray<int16> Saved;
		auto& Mutable = const_cast<FVoxelLandM5ScratchChunks&>(Source);
		for (auto& Chunk : Mutable.Chunks)
		{
			for (int32 Z = 12; Z <= 14; ++Z)
			{
				for (int32 Y = 4; Y <= 6; ++Y)
				{
					int16& Density = Chunk.Density[(6 + 1) + 35 * ((Y + 1) + 35 * (Z + 1))];
					Saved.Add(Density);
					Density = -100;
				}
			}
		}
		return Saved;
	}
	static void RestoreWall(const FVoxelLandM5ScratchChunks& Source, const TArray<int16>& Saved)
	{
		int32 Index = 0;
		auto& Mutable = const_cast<FVoxelLandM5ScratchChunks&>(Source);
		for (auto& Chunk : Mutable.Chunks)
		{
			for (int32 Z = 12; Z <= 14; ++Z)
			{
				for (int32 Y = 4; Y <= 6; ++Y)
				{
					Chunk.Density[(6 + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Saved[Index++];
				}
			}
		}
	}
	static void ShiftPlan(const FVoxelLandM5CandidateResult& Source, int64 Delta)
	{
		auto& Plan = const_cast<FVoxelLandM5CandidateResult&>(Source).Plan;
		for (auto& Chamber : Plan.Chambers)
		{
			Chamber.CenterCm.X += Delta;
		}
		for (auto& Segment : Plan.Segments)
		{
			Segment.StartCm.X += Delta;
			Segment.EndCm.X += Delta;
		}
	}
	static TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Make(
		TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Baseline,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, bool bSeam = false)
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
		A.CenterCm.X = bSeam ? -150 : 475;
		A.CenterCm.Y = bSeam ? 1450 : 530;
		A.CenterCm.Z = 1150;
		A.RadiusCm = 100;
		FVoxelLandM5Chamber B = A;
		B.Id = 2;
		B.CenterCm.X = bSeam ? 150 : 675;
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
			MakeShareable(new FVoxelLandM5CandidateResult(
				Budget, Snapshot.Caves, Snapshot.Base.Seed, Region, MoveTemp(Plan), MoveTemp(PlanLease)));
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
	Async(EAsyncExecution::ThreadPool, [&] {
		bControl = FVoxelLandM5CapturedRoutesFixture::MakeControl(Control, Error);
	}).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5CapturedRoutesFixture::Apply(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorldSource> Definition(NewObject<UVoxelLandFiniteWorldSource>());
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
	Definition->Common.Caves.Capsule.ClearanceMarginCm = 10;
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
	// Mode zero admits the extra adversarial re-meshing checks; the positive
	// one- and two-chunk paths below use the unmodified default work cap.
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
		AddError(TEXT("Captured chunks fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
							 TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result;
		if (Generator.CreateWorker(Worker))
		{
			Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return Result;
	};
	const auto Never = [] {
		return false;
	};
	bool bNetwork = false, bHalo = false, bWrongMode = false, bCancelled = false, bLifetime = false;
	bool bMesh = false, bMismatch = false, bBounds = false, bRouteCancelled = false, bConnectedRoute = false;
	bool bSupport = false, bSupportCancelled = false, bMiddleObstruction = false, bOmittedOwner = false;
	bool bDefaultCapture = false, bSeamCapture = false, bBoxCancel = false;
	bool bBoxForeign = false, bBoxThread = false, bBoxInvalid = false;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		Definition->Common.Caves.bEnabled = true;
		Definition->Common.Caves.Pockets.bEnabled = false;
		Definition->Common.Caves.Pockets.AmplitudeCm = 0;
		Definition->Common.Caves.Limits.MaxValidationWork =
			Mode == 0 ? 1000000000 : FVoxelLandM5ResourceLimits().MaxValidationWork;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(
				Definition->Common.Caves.Limits.MaxBytes, Definition->Common.Caves.Limits.MaxValidationWork, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
		const FVoxelLandM5Diagnostic Admission =
			FVoxelLandM5BaseCapture::Capture(*Definition.Get(), Budget.ToSharedRef(), Capture);
		if (Admission.Code == EVoxelLandM5ErrorCode::BudgetExceeded
			&& Admission.Message == TEXT("M4 preparation reservation unavailable"))
		{
			AddError(TEXT("Captured chunks fixture requires an available M4 process admission"));
			return false;
		}
		if (!TestTrue(FString::Printf(TEXT("captured source: %s"), *Admission.Message),
				Admission.Code == EVoxelLandM5ErrorCode::None && Capture.IsValid()))
		{
			return false;
		}
		FString Failure;
		Async(EAsyncExecution::ThreadPool, [&] {
			TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
			FVoxelLandM5Diagnostic Result =
				FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Scratch)
			{
				Failure = Result.Message;
				return;
			}
			// Stored footprint plus four mask-neighborhood samples on each side.
			// Seam case covers both X chunks using one exact captured baseline.
			TUniquePtr<FVoxelLandWorkerContext> BoxWorker;
			if (Scratch->GetScratch().GetGenerator().CreateWorker(BoxWorker) || !BoxWorker)
			{
				return;
			}
			FVoxelLandM5PointCm BoxMin;
			BoxMin.X = Mode == 2 ? -37 : -5;
			BoxMin.Y = BoxMin.Z = -5;
			const FIntVector BoxSize(Mode == 2 ? 75 : 43, 43, 43);
			TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source;
			Result = FVoxelLandM5CapturedBaseline::BuildBox(
				Scratch.ToSharedRef(), *BoxWorker, BoxMin, BoxSize, Never, Source);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Source)
			{
				Failure = Result.Message;
				return;
			}
			if (Mode == 0)
			{
				const auto Before = Source;
				const auto CancelBox = FVoxelLandM5CapturedBaseline::BuildBox(
					Scratch.ToSharedRef(), *BoxWorker, BoxMin, BoxSize, [] { return true; }, Source);
				bBoxCancel = CancelBox.Code == EVoxelLandM5ErrorCode::Cancelled && Source == Before;
				const auto InvalidBox = FVoxelLandM5CapturedBaseline::BuildBox(
					Scratch.ToSharedRef(), *BoxWorker, BoxMin, FIntVector(0, 43, 43), Never, Source);
				bBoxInvalid = InvalidBox.Code == EVoxelLandM5ErrorCode::InvalidInput && Source == Before;
				FVoxelLandProgramDraft Draft;
				Draft.RegisterCount = 1;
				FVoxelLandInstruction Input, Store;
				Input.Op = EVoxelLandOpcode::LoadZ;
				Store.Op = EVoxelLandOpcode::StoreDensity;
				Draft.Instructions = {Input, Store};
				TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
				FVoxelLandGeneratorSettings OtherSettings;
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> OtherGenerator;
				TUniquePtr<FVoxelLandWorkerContext> OtherWorker;
				if (!FVoxelLandProgram::Validate(Draft, Program) && Program)
				{
					OtherSettings.Program = Program;
					if (!FVoxelLandGenerator::Create(OtherSettings, OtherGenerator) && OtherGenerator
						&& !OtherGenerator->CreateWorker(OtherWorker) && OtherWorker)
					{
						int64 LiveBefore = 0, PeakBefore = 0, WorkBefore = 0, LiveAfter = 0, PeakAfter = 0,
							  WorkAfter = 0;
						Budget->GetUsage(LiveBefore, PeakBefore, WorkBefore);
						const auto Foreign = FVoxelLandM5CapturedBaseline::BuildBox(
							Scratch.ToSharedRef(), *OtherWorker, BoxMin, BoxSize, Never, Source);
						Budget->GetUsage(LiveAfter, PeakAfter, WorkAfter);
						bBoxForeign = Foreign.Code == EVoxelLandM5ErrorCode::InvalidInput && Source == Before
								   && LiveBefore == LiveAfter && PeakBefore == PeakAfter && WorkBefore == WorkAfter;
						const auto WrongThread = Async(EAsyncExecution::Thread, [&] {
							return FVoxelLandM5CapturedBaseline::BuildBox(
								Scratch.ToSharedRef(), *BoxWorker, BoxMin, BoxSize, Never, Source);
						}).Get();
						Budget->GetUsage(LiveAfter, PeakAfter, WorkAfter);
						bBoxThread = WrongThread.Code == EVoxelLandM5ErrorCode::InvalidInput && Source == Before
								  && LiveBefore == LiveAfter && PeakBefore == PeakAfter && WorkBefore == WorkAfter;
					}
				}
			}
			TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate =
				FVoxelLandM5CapturedMeshRoutesFixture::Make(Source.ToSharedRef(), Budget.ToSharedRef(), Mode == 2);
			if (!Candidate)
			{
				Failure = TEXT("Controlled connected candidate admission failed");
				return;
			}
			TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Chunks;
			Result = FVoxelLandM5CapturedScratchChunks::BuildNetwork(Candidate.ToSharedRef(),
				FIntVector(Mode == 2 ? -1 : 0, 0, 0), FIntVector(1, 1, 1), Mode == 2 ? 2 : 1, Never, Chunks);
			Failure = Result.Message;
			const FVoxelLandM5WorldSnapshot& Snapshot = Capture->GetSnapshot();
			const bool bMatches =
				Result.Code == EVoxelLandM5ErrorCode::None && Chunks && &Chunks->GetSource() == Source.Get()
				&& Chunks->GetCandidate() == Candidate.Get() && Chunks->GetChunks().IsOwnedBy(*Budget)
				&& Chunks->GetChunks().GetChunks().Num() == (Mode == 2 ? 2 : 1)
				&& Chunks->GetChunks().MatchesDependencies(Source->GetWindow(),
					Candidate ? &Candidate->GetCandidate() : nullptr, nullptr, Snapshot.Base.Seed, Snapshot.Manifest)
				&& Chunks->GetChunks().MatchesSettings(Snapshot.Caves);
			if (Mode == 0)
			{
				bNetwork = bMatches;
			}
			if (bMatches && Candidate)
			{
				TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
				const FVoxelLandM5Diagnostic Meshed = FVoxelLandM5ScratchMesh::Build(
					Chunks->GetChunksShared(), Mode == 2 ? 2 : 1, 10000, Budget.ToSharedRef(), Never, Mesh);
				Failure = Meshed.Message;
				bMesh = Meshed.Code == EVoxelLandM5ErrorCode::None && Mesh && Mesh->GetTriangles().Num() > 0;
				if (bMesh)
				{
					TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> Routes;
					const FVoxelLandM5Diagnostic Connected = FVoxelLandM5CapturedMeshRoutes::Build(
						Chunks.ToSharedRef(), Mesh.ToSharedRef(), 200, Never, Routes);
					Failure = Connected.Message;
					if (Mode != 0)
					{
						if (Connected.Code == EVoxelLandM5ErrorCode::None && Routes)
						{
							const auto Check = Routes->CheckSupport(0, 256, Never);
							Failure = Check.Message;
							if (Mode == 1)
							{
								bDefaultCapture = Check.Code == EVoxelLandM5ErrorCode::None;
							}
							else
							{
								const auto& Plan = Routes->GetCandidate().GetPlan();
								bSeamCapture = Check.Code == EVoxelLandM5ErrorCode::None && Plan.Routes.Num() == 1
											&& Plan.Routes[0].Legs.Num() == 1
											&& Plan.Routes[0].Legs[0].StartFeetCm.X == -150
											&& Plan.Routes[0].Legs[0].EndFeetCm.X == 150
											&& Plan.Routes[0].Legs[0].StartFeetCm.Y == 1450
											&& Plan.Routes[0].Legs[0].EndFeetCm.Y == 1450
											&& Plan.Routes[0].Legs[0].StartFeetCm.Z == 1050
											&& Plan.Routes[0].Legs[0].EndFeetCm.Z == 1050;
							}
						}
						return;
					}
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
					if (!bConnectedRoute)
					{
						return;
					}
					const auto Support = Routes->CheckSupport(0, 256, Never);
					bSupport = Support.Code == EVoxelLandM5ErrorCode::None;
					if (!bSupport)
					{
						Failure = Support.Message;
					}
					const auto CancelSupport = Routes->CheckSupport(0, 256, [] { return true; });
					bSupportCancelled = CancelSupport.Code == EVoxelLandM5ErrorCode::Cancelled;
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
					// Re-mesh a controlled suspended wall crossing the middle of the leg.
					const TArray<int16> SavedDensity =
						FVoxelLandM5CapturedMeshRoutesFixture::SetWall(Chunks->GetChunks());
					TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> WallMesh;
					TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> WallRoutes;
					const auto WallBuild = FVoxelLandM5ScratchMesh::Build(
						Chunks->GetChunksShared(), 1, 10000, Budget.ToSharedRef(), Never, WallMesh);
					if (WallBuild.Code == EVoxelLandM5ErrorCode::None && WallMesh
						&& FVoxelLandM5CapturedMeshRoutes::Build(
							   Chunks.ToSharedRef(), WallMesh.ToSharedRef(), 200, Never, WallRoutes)
								   .Code
							   == EVoxelLandM5ErrorCode::None
						&& WallRoutes)
					{
						const auto WallCheck = WallRoutes->CheckSupport(0, 256, Never);
						bMiddleObstruction = WallCheck.Code == EVoxelLandM5ErrorCode::Unproven
										  && WallCheck.Stage == TEXT("M5.SweptTriangle");
					}
					FVoxelLandM5CapturedMeshRoutesFixture::RestoreWall(Chunks->GetChunks(), SavedDensity);
					// Feet search stays inside its owner rectangle, but the capsule sweep does not.
					FVoxelLandM5CapturedMeshRoutesFixture::ShiftPlan(Candidate->GetCandidate(), -250);
					TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe> EdgeRoutes;
					if (FVoxelLandM5CapturedMeshRoutes::Build(
							Chunks.ToSharedRef(), Mesh.ToSharedRef(), 200, Never, EdgeRoutes)
								.Code
							== EVoxelLandM5ErrorCode::None
						&& EdgeRoutes)
					{
						const auto EdgeCheck = EdgeRoutes->CheckSupport(0, 256, Never);
						bOmittedOwner = EdgeCheck.Code == EVoxelLandM5ErrorCode::Unproven
									 && EdgeCheck.Message == TEXT("Sweep requires omitted neighboring mesh owners");
					}
					FVoxelLandM5CapturedMeshRoutesFixture::ShiftPlan(Candidate->GetCandidate(), 250);
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
		TestTrue(FString::Printf(TEXT("captured route mode %d: %s"), Mode, *Failure), Mode == 0   ? bNetwork
																					  : Mode == 1 ? bDefaultCapture
																								  : bSeamCapture);
		Capture.Reset();
	}
	TestTrue(TEXT("actual captured scratch mesh"), bMesh);
	TestTrue(TEXT("foreign scratch mesh rejected"), bMismatch);
	TestTrue(TEXT("invalid search depth rejected"), bBounds);
	TestTrue(TEXT("route cancellation preserves output"), bRouteCancelled);
	TestTrue(TEXT("controlled connected source resolves actual mesh feet"), bConnectedRoute);
	TestTrue(TEXT("captured route passes retained CPU support and positive-margin clearance"), bSupport);
	TestTrue(TEXT("captured route support observes cancellation"), bSupportCancelled);
	TestTrue(TEXT("captured baseline box cancellation preserves output"), bBoxCancel);
	TestTrue(TEXT("captured baseline box rejects invalid size atomically"), bBoxInvalid);
	TestTrue(TEXT("captured baseline box rejects foreign worker before admission"), bBoxForeign);
	TestTrue(TEXT("captured baseline box rejects wrong thread before admission"), bBoxThread);
	TestTrue(TEXT("captured one-chunk route fits default admission"), bDefaultCapture);
	TestTrue(TEXT("captured seam route fits default admission"), bSeamCapture);
	TestTrue(TEXT("middle obstruction fails retained obstacle sweep"), bMiddleObstruction);
	TestTrue(TEXT("omitted owner rejects capsule sweep"), bOmittedOwner);
	TestTrue(TEXT("network mode rejects pocket entry"), bWrongMode);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("missing halo preserves output"), bHalo);
	TestTrue(TEXT("chunks retain captured source"), bLifetime);
	return bNetwork && bMesh && bMismatch && bBounds && bRouteCancelled && bConnectedRoute && bSupport
		&& bSupportCancelled && bMiddleObstruction && bOmittedOwner && bBoxCancel && bBoxInvalid && bBoxForeign
		&& bBoxThread && bDefaultCapture && bSeamCapture;
}
#endif
