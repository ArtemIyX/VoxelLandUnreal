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
#include "World/Caves/Planning/VoxelLandCaveFinitePrepass.h"

class FVoxelLandM5FinitePrepassFixtureLease final : public FVoxelLandM4GridLease
{
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FinitePrepassTest, "VoxelLand.M5.FinitePrepass.ControlsToGrid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FinitePrepassTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bMissingWorker = false, bFactoryFailure = false, bCancelled = false;
	bool bLease = false, bNoLease = false, bMismatch = false, bAfterWorkerCancel = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Snapshot;
		Snapshot.Domain.CellsX = Snapshot.Domain.CellsY = 1;
		Snapshot.Domain.OriginXCm = Snapshot.Domain.OriginYCm = -100;
		Snapshot.Domain.GridSpacingCm = 100;
		Snapshot.Domain.bGlobalErosion = false;
		Snapshot.BaseHeightQ8 = 5;
		Snapshot.OverhangNodeId = FGuid(8, 7, 6, 5);
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
			MakeShared<FVoxelLandM5FinitePrepassFixtureLease, ESPMode::ThreadSafe>();
		TWeakPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> WeakProcess = Process;
		int32 Calls = 0;
		const auto Factory = [&Calls](const FVoxelLandGenerator& Generator,
								 TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
								 TUniquePtr<FVoxelLandWorkerContext>& Worker) {
			++Calls;
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
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		uint64 Hash = 77;
		const auto Success =
			VoxelLand::M5::FinitePrepass::Generate(Snapshot, Budget.ToSharedRef(), Process, Factory, Never, Grid, Hash);
		Error = Success.Message;
		int32 Height = 0;
		bSuccess = Success.Code == EVoxelLandM5ErrorCode::None && Grid && Calls == 3 && Grid->Sample(-100, -100, Height)
				&& Height == 5;
		const auto Original = Grid;
		const uint64 OriginalHash = Hash;
		const auto EmptyFactory = [](const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
									  TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&) {
			return FVoxelLandM5Diagnostic();
		};
		const auto Missing = VoxelLand::M5::FinitePrepass::Generate(
			Snapshot, Budget.ToSharedRef(), Process, EmptyFactory, Never, Grid, Hash);
		bMissingWorker =
			Missing.Code == EVoxelLandM5ErrorCode::InvalidInput && Grid == Original && Hash == OriginalHash;
		const auto BadFactory = [](const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
									TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&) {
			FVoxelLandM5Diagnostic Result;
			Result.Code = EVoxelLandM5ErrorCode::Unproven;
			return Result;
		};
		const auto Bad = VoxelLand::M5::FinitePrepass::Generate(
			Snapshot, Budget.ToSharedRef(), Process, BadFactory, Never, Grid, Hash);
		bFactoryFailure = Bad.Code == EVoxelLandM5ErrorCode::Unproven && Grid == Original && Hash == OriginalHash;
		const FVoxelLandGenerator* FirstGenerator = nullptr;
		const auto WrongFactory = [&FirstGenerator](const FVoxelLandGenerator& Generator,
									  TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
									  TUniquePtr<FVoxelLandWorkerContext>& Worker) {
			if (!FirstGenerator)
			{
				FirstGenerator = &Generator;
			}
			FVoxelLandM5Diagnostic Result;
			if (FirstGenerator->CreateWorker(Worker))
			{
				Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
			}
			return Result;
		};
		const auto Wrong = VoxelLand::M5::FinitePrepass::Generate(
			Snapshot, Budget.ToSharedRef(), Process, WrongFactory, Never, Grid, Hash);
		bMismatch = Wrong.Code != EVoxelLandM5ErrorCode::None && Grid == Original && Hash == OriginalHash;
		bool bCancelAfterWorker = false;
		const auto CancelFactory = [&bCancelAfterWorker](const FVoxelLandGenerator& Generator,
									   TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
									   TUniquePtr<FVoxelLandWorkerContext>& Worker) {
			FVoxelLandM5Diagnostic Result;
			if (Generator.CreateWorker(Worker))
			{
				Result.Code = EVoxelLandM5ErrorCode::InvalidInput;
			}
			bCancelAfterWorker = true;
			return Result;
		};
		const auto MidCancel = VoxelLand::M5::FinitePrepass::Generate(
			Snapshot, Budget.ToSharedRef(), Process, CancelFactory,
			[&bCancelAfterWorker] { return bCancelAfterWorker; }, Grid, Hash);
		bAfterWorkerCancel =
			MidCancel.Code == EVoxelLandM5ErrorCode::Cancelled && Grid == Original && Hash == OriginalHash;
		const auto Cancel = VoxelLand::M5::FinitePrepass::Generate(
			Snapshot, Budget.ToSharedRef(), Process, Factory, [] { return true; }, Grid, Hash);
		bCancelled = Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Grid == Original && Hash == OriginalHash;
		const auto NoLease =
			VoxelLand::M5::FinitePrepass::Generate(Snapshot, Budget.ToSharedRef(), nullptr, Factory, Never, Grid, Hash);
		bNoLease = NoLease.Code == EVoxelLandM5ErrorCode::InvalidInput && Grid == Original && Hash == OriginalHash;
		Process.Reset();
		bLease = WeakProcess.IsValid() && Grid.IsValid();
	}).Wait();
	TestTrue(FString::Printf(TEXT("snapshot to grid: %s"), *Error), bSuccess);
	TestTrue(TEXT("missing worker preserves outputs"), bMissingWorker);
	TestTrue(TEXT("worker failure preserves outputs"), bFactoryFailure);
	TestTrue(TEXT("mismatched worker is rejected atomically"), bMismatch);
	TestTrue(TEXT("cancellation after worker creation preserves outputs"), bAfterWorkerCancel);
	TestTrue(TEXT("cancellation preserves outputs"), bCancelled);
	TestTrue(TEXT("missing process lease preserves outputs"), bNoLease);
	TestTrue(TEXT("output retains process lease"), bLease);
	return bSuccess;
}
#endif
