#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"

class FVoxelLandM5FiniteBaseScratchFixtureLease final : public FVoxelLandM4GridLease
{
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteBaseScratchTest, "VoxelLand.M5.FiniteBaseScratch.Assembly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteBaseScratchTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bAtomic = false, bCancelled = false, bLease = false, bFactoryFailure = false;
	FString Error;
	TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> WeakProcess;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Snapshot;
		Snapshot.Domain.CellsX = Snapshot.Domain.CellsY = 128;
		Snapshot.Domain.OriginXCm = Snapshot.Domain.OriginYCm = -6400;
		Snapshot.Domain.GridSpacingCm = 100;
		Snapshot.Domain.bGlobalErosion = false;
		Snapshot.BaseHeightQ8 = 5;
		Snapshot.OverhangNodeId = FGuid(8, 7, 6, 5);
		Snapshot.Seed = 9123;
		Snapshot.bNormalizeSlope = true;
		Snapshot.bOverhangEnabled = true;
		Snapshot.OverhangAmplitudeQ8 = 1000;
		Snapshot.OverhangSalt = 17;
		Snapshot.HeightDetail.bEnabled = true;
		Snapshot.HeightDetail.AmplitudeQ8 = 128000;
		Snapshot.HeightDetail.FrequencyPerCm = 0.004f;
		Snapshot.HeightDetail.Salt = 31;
		Snapshot.Smooth.bEnabled = true;
		Snapshot.Smooth.Strength = 0.5f;
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
		uint64 Fingerprint = 0;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> ContinentalProgram;
		FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source.Nodes = {Constant, Output};
			Channel->Source.Links = {Link};
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(Channel->Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Program) || !Program
				|| !FVoxelLandProgramCodec::Pack(Channel->Source, *Program, Channel->Payload, Error))
			{
				return;
			}
			Fingerprint = Program->GetFingerprint();
			if (Channel == &Snapshot.Continental)
			{
				ContinentalProgram = Program;
			}
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			Channel->CurveKeys = {First, Last};
			Channel->CookedCurve.SourceKeys = Channel->CurveKeys;
			Channel->CookedCurve.BakeVersion = 1;
			Channel->CookedCurve.SamplesQ8.Init(0, FVoxelLandM4CurveLut::SampleCount);
			FCanonicalWriter Writer;
			for (int32 Value : Channel->CookedCurve.SamplesQ8)
			{
				Writer.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
			FMemory::Memcpy(&Channel->CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(128ll * 1024 * 1024, 100000000, Budget))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Process =
			MakeShared<FVoxelLandM5FiniteBaseScratchFixtureLease, ESPMode::ThreadSafe>();
		WeakProcess = Process;
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
		const auto Never = [] {
			return false;
		};
		TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch;
		const FVoxelLandM5Diagnostic Built =
			FVoxelLandM5FiniteBaseScratch::Build(Snapshot, Budget.ToSharedRef(), Process, Factory, Never, Scratch);
		Error = Built.Message;
		int32 Height = 0;
		bSuccess = Built.Code == EVoxelLandM5ErrorCode::None && Scratch && Scratch->GetGrid().Sample(-100, -100, Height)
				&& Height == 5 && Scratch->GetGenerator().GetProgram().GetFingerprint() == Fingerprint
				&& Scratch->GetGenerator().GetProfile().VoxelSizeCm == 100.f;
		if (!bSuccess)
		{
			return;
		}
		// Build an independent ordinary M4 generator over the expected constant
		// prepass grid; compare complete chunks with independently owned workers.
		TArray<int32> ExpectedHeights;
		ExpectedHeights.Init(5, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> ExpectedGrid;
		if (!FVoxelLandM4HeightGrid::Create(-6400, -6400, 128, 128, 100, ExpectedHeights, ExpectedGrid))
		{
			bSuccess = false;
			return;
		}
		FVoxelLandGeneratorSettings ExpectedSettings;
		ExpectedSettings.Program = ContinentalProgram;
		ExpectedSettings.WorldSeed = uint64(Snapshot.Seed);
		ExpectedSettings.Profile.VoxelSizeCm = float(Snapshot.VoxelSizeCm);
		FVoxelLandM4OverhangConfig ExpectedOverhang;
		ExpectedOverhang.Displacement.bEnabled = Snapshot.bOverhangEnabled;
		ExpectedOverhang.Displacement.AmplitudeQ8 = Snapshot.OverhangAmplitudeQ8;
		ExpectedOverhang.Displacement.BandHalfWidthQ8 = Snapshot.OverhangBandQ8;
		ExpectedOverhang.FrequencyPerCm = Snapshot.OverhangFrequencyPerCm;
		ExpectedOverhang.NodeId = Snapshot.OverhangNodeId;
		ExpectedOverhang.Salt = uint32(Snapshot.OverhangSalt);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> ExpectedGenerator;
		TUniquePtr<FVoxelLandWorkerContext> ActualWorker, ExpectedWorker;
		if (FVoxelLandGenerator::CreateM4HeightField(ExpectedSettings, ExpectedGrid, Snapshot.bNormalizeSlope,
				ExpectedOverhang, Snapshot.Smooth, Snapshot.HeightDetail, ExpectedGenerator)
			|| !ExpectedGenerator || Scratch->GetGenerator().CreateWorker(ActualWorker)
			|| ExpectedGenerator->CreateWorker(ExpectedWorker))
		{
			bSuccess = false;
			return;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(-1, -1, -1);
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ActualChunk, ExpectedChunk;
		bSuccess = !Scratch->GetGenerator().GenerateChunk(Key, *ActualWorker, ActualChunk)
				&& !ExpectedGenerator->GenerateChunk(Key, *ExpectedWorker, ExpectedChunk) && ActualChunk
				&& ExpectedChunk && ActualChunk->Density == ExpectedChunk->Density
				&& ActualChunk->Hash == ExpectedChunk->Hash;
		const TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Previous = Scratch;
		Snapshot.Smooth.Version = 2;
		const FVoxelLandM5Diagnostic Invalid =
			FVoxelLandM5FiniteBaseScratch::Build(Snapshot, Budget.ToSharedRef(), Process, Factory, Never, Scratch);
		bAtomic = Invalid.Code != EVoxelLandM5ErrorCode::None && Scratch == Previous;
		Snapshot.Smooth.Version = 1;
		const auto FailFactory = [](const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
									 TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&) {
			FVoxelLandM5Diagnostic Result;
			Result.Code = EVoxelLandM5ErrorCode::Unproven;
			return Result;
		};
		const FVoxelLandM5Diagnostic FactoryFailure =
			FVoxelLandM5FiniteBaseScratch::Build(Snapshot, Budget.ToSharedRef(), Process, FailFactory, Never, Scratch);
		bFactoryFailure = FactoryFailure.Code == EVoxelLandM5ErrorCode::Unproven && Scratch == Previous;
		const FVoxelLandM5Diagnostic Cancelled = FVoxelLandM5FiniteBaseScratch::Build(
			Snapshot, Budget.ToSharedRef(), Process, Factory, [] { return true; }, Scratch);
		bCancelled = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Scratch == Previous;
		Process.Reset();
		bLease = WeakProcess.IsValid();
	}).Wait();
	TestTrue(FString::Printf(TEXT("finite scratch from snapshot: %s"), *Error), bSuccess);
	TestTrue(TEXT("invalid snapshot preserves output"), bAtomic);
	TestTrue(TEXT("worker factory failure preserves output"), bFactoryFailure);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("scratch retains and releases process lease"), bLease && !WeakProcess.IsValid());
	return bSuccess;
}
#endif
