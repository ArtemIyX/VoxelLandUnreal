#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Generation/VoxelLandM5BaselineMesh.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
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
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FVoxelLandM5BaselineMesh>);
static_assert(!std::is_copy_assignable_v<FVoxelLandM5BaselineMesh>);
namespace
{
bool MakeBaselineMeshControl(FVoxelLandM4ControlSnapshot& Out, FString& Error, bool bRamp = false)
{
	FVoxelLandGraphNodeRecord Constant, Output;
	Constant.Id = FGuid(1, 1, 1, 1); Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
	Constant.Pins.SetNum(2); Constant.Pins[0].Id = FGuid(2, 1, 1, 1); Constant.Pins[1].Id = FGuid(2, 1, 1, 2); Constant.Pins[1].bOutput = true;
	Output.Id = FGuid(1, 1, 1, 2); Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	Output.Pins.SetNum(1); Output.Pins[0].Id = FGuid(2, 1, 1, 3);
	FVoxelLandGraphLinkRecord Link; Link.SourceNode = Constant.Id; Link.SourcePin = Constant.Pins[1].Id;
	Link.TargetNode = Output.Id; Link.TargetPin = Output.Pins[0].Id;
	if (bRamp)
	{
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionX);
		Constant.Pins.RemoveAt(0);
	}
	Out.Source.Nodes = {Constant, Output}; Out.Source.Links = {Link};
	FVoxelLandProgramDraft Draft; TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (!FVoxelLandGraphCompiler::Compile(Out.Source, Draft, Error) || FVoxelLandProgram::Validate(Draft, Program) || !Program
		|| !FVoxelLandProgramCodec::Pack(Out.Source, *Program, Out.Payload, Error)) return false;
	FVoxelLandM4CurveAuthoringKey First, Last; Last.InputQ16 = 65535;
	Out.CurveKeys = {First, Last}; Out.CookedCurve.SourceKeys = Out.CurveKeys; Out.CookedCurve.BakeVersion = 1;
	if (bRamp)
	{
		Out.CurveKeys[0].OutputQ8 = -400 * 256; Out.CurveKeys[1].OutputQ8 = 400 * 256;
		Out.CookedCurve.SourceKeys = Out.CurveKeys;
		FVoxelLandM4CurveKey A, B; A.InputQ16 = 0; A.Output = -400 * 256;
		B.InputQ16 = 65535; B.Output = 400 * 256;
		TArray<FVoxelLandM4CurveKey> Keys{A, B}; TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		if (!VoxelLandM4CurveBake::Bake(Keys, -400 * 256, 400 * 256, Out.CookedCurve.SamplesQ8, Curve, Error)) return false;
	}
	else Out.CookedCurve.SamplesQ8.Init(0, 4097);
	FCanonicalWriter Writer;
	for (int32 Sample : Out.CookedCurve.SamplesQ8) Writer.U32(uint32(Sample));
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	FMemory::Memcpy(&Out.CookedCurve.SamplesHash, &Hash, sizeof(Hash)); return true;
}
void ApplyBaselineMeshControl(UVoxelLandM4WorldConfig& Owner, FVoxelLandM4ControlConfig& Target, const FVoxelLandM4ControlSnapshot& Source)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Owner);
	Graph->SourceVersion = Source.Source.SourceVersion; Graph->Parameters = Source.Source.Parameters;
	Graph->Nodes = Source.Source.Nodes; Graph->Links = Source.Source.Links; Graph->CompiledProgram = Source.Payload;
	Target.Graph = Graph; Target.CurveKeys = Source.CurveKeys; Target.CookedCurve = Source.CookedCurve;
}
bool EqualPoint(const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}
bool HasSharedOwnerSeam(const FVoxelLandM5BaselineMesh& Mesh)
{
	TSet<FString> NegativeVertices, ZeroVertices, NegativeEdges, ZeroEdges;
	for (int32 I = 0; I < Mesh.GetTriangles().Num(); ++I)
	{
		const int32 Owner = Mesh.GetFaces()[I].ChunkCoordinate.X;
		if (Owner != -1 && Owner != 0) continue;
		const auto& Triangle = Mesh.GetTriangles()[I];
		const FVoxelLandM5FixedPointQ18* Points[] = {&Triangle.A, &Triangle.B, &Triangle.C};
		FString Keys[3];
		for (int32 P = 0; P < 3; ++P)
		{
			const auto* Point = Points[P];
			if (Point->X < -int64(100) * 262144 || Point->X > int64(100) * 262144) continue;
			Keys[P] = FString::Printf(TEXT("%lld,%lld,%lld"), Point->X, Point->Y, Point->Z);
			(Owner == -1 ? NegativeVertices : ZeroVertices).Add(Keys[P]);
		}
		for (int32 P = 0; P < 3; ++P)
		{
			const FString& A = Keys[P]; const FString& B = Keys[(P + 1) % 3];
			if (A.IsEmpty() || B.IsEmpty() || A == B) continue;
			const FString Edge = A < B ? A + TEXT("|") + B : B + TEXT("|") + A;
			(Owner == -1 ? NegativeEdges : ZeroEdges).Add(Edge);
		}
	}
	int32 Shared = 0;
	for (const FString& Vertex : NegativeVertices) if (ZeroVertices.Contains(Vertex)) ++Shared;
	bool bSharedEdge = false;
	for (const FString& Edge : NegativeEdges) bSharedEdge |= ZeroEdges.Contains(Edge);
	return Shared >= 2 && bSharedEdge;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BaselineMeshRealTest, "VoxelLand.M5.BaselineMesh.RealFinite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BaselineMeshRealTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture runs on game thread"), IsInGameThread())) return false;
	FVoxelLandM4ControlSnapshot Control; FString Error; bool bControl = false;
	Async(EAsyncExecution::ThreadPool, [&] { bControl = MakeBaselineMeshControl(Control, Error); }).Wait();
	if (!TestTrue(FString::Printf(TEXT("control setup: %s"), *Error), bControl)) return false;
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	ApplyBaselineMeshControl(*Config.Get(), Config->Continental, Control);
	ApplyBaselineMeshControl(*Config.Get(), Config->Mountain, Control);
	ApplyBaselineMeshControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental; Asset->Common.Mountain = Config->Mountain; Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345; Asset->Common.BaseHeightQ8 = 1050 * 256;
	Asset->Common.Caves.bEnabled = true; Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.Limits.MaxValidationWork = 100000000;
	Asset->Finite.bGlobalErosion = false; Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800; Asset->Finite.GridSpacingCm = 6400;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Baseline mesh fixture requires the M4 process budget to be 256 MiB")); return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Source, Job, Foreign;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Source)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Job)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, Foreign)) return false;
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const auto Captured = FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Source.ToSharedRef(), Capture);
	if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message), Captured.Code == EVoxelLandM5ErrorCode::None && Capture)) return false;
	const auto Never = [] { return false; };
	const auto Factory = [](const FVoxelLandGenerator& Generator, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>& Worker) {
		FVoxelLandM5Diagnostic Result; if (Generator.CreateWorker(Worker)) Result.Code = EVoxelLandM5ErrorCode::InvalidInput; return Result;
	};
	bool bBuilt = false, bFlat = false, bOwners = false, bRange = false, bOutside = false;
	bool bBudget = false, bCancel = false, bWorker = false, bLimit = false, bFaces = false, bSeam = false, bSharedSeam = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Capture->GetSnapshot());
		auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
		Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -4000;
		Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 4000;
		FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
		auto Result = FVoxelLandM5DefinitionValidator::Validate(Inputs, Source.ToSharedRef(), Never, Definition);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Definition) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
		Result = FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), Source.ToSharedRef(), Never, Influence);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Influence) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
		Result = FVoxelLandM5CapturedFiniteScratch::BuildBound(Capture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Baseline) { Failure = Result.Message; return; }
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) return;
		const FIntVector Min(-1, 0, 0), Max(1, 1, 1);
		TSharedPtr<const FVoxelLandM5BaselineMesh, ESPMode::ThreadSafe> Mesh;
		Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
			*Worker, Min, Max, 2, 196608, Job.ToSharedRef(), Never, Mesh);
		Failure = Result.Message;
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Mesh) return;
		bBuilt = Mesh->GetTriangles().Num() > 0 && Mesh->GetTriangles().Num() == Mesh->GetFaces().Num();
		bRange = Mesh->GetMinInclusive() == Min && Mesh->GetMaxExclusive() == Max;
		bOwners = Mesh->Matches(*Baseline, *Influence, *Source, *Job) && !Mesh->Matches(*Baseline, *Influence, *Foreign, *Job);
		bFlat = true; bool bNegative = false, bPositive = false;
		for (int32 I = 0; I < Mesh->GetTriangles().Num(); ++I)
		{
			const auto& Triangle = Mesh->GetTriangles()[I];
			constexpr int64 HeightQ18 = int64(1050) * 262144;
			bFlat &= Triangle.A.Z == HeightQ18 && Triangle.B.Z == HeightQ18 && Triangle.C.Z == HeightQ18;
			const auto& Face = Mesh->GetFaces()[I];
			bNegative |= Face.ChunkCoordinate == FIntVector(-1, 0, 0);
			bPositive |= Face.ChunkCoordinate == FIntVector(0, 0, 0);
		}
		bFaces = bNegative && bPositive;
		bSharedSeam = HasSharedOwnerSeam(*Mesh);
		// Generate each owner independently and compare its complete face stream
		// with the same owner in the two-chunk build across the x=0 seam.
		bSeam = true;
		for (int32 X = -1; X <= 0; ++X)
		{
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SingleJob;
			if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, SingleJob)) { bSeam = false; break; }
			TSharedPtr<const FVoxelLandM5BaselineMesh, ESPMode::ThreadSafe> Single;
			Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, FIntVector(X, 0, 0), FIntVector(X + 1, 1, 1), 1, 196608, SingleJob.ToSharedRef(), Never, Single);
			if (Result.Code != EVoxelLandM5ErrorCode::None || !Single) { bSeam = false; break; }
			int32 At = 0;
			for (int32 I = 0; I < Mesh->GetFaces().Num(); ++I)
			{
				if (Mesh->GetFaces()[I].ChunkCoordinate.X != X) continue;
				if (At >= Single->GetFaces().Num()) { bSeam = false; break; }
				const auto& A = Mesh->GetTriangles()[I]; const auto& B = Single->GetTriangles()[At];
				bSeam &= Mesh->GetFaces()[I].FaceIndex == Single->GetFaces()[At].FaceIndex
					&& EqualPoint(A.A, B.A) && EqualPoint(A.B, B.B) && EqualPoint(A.C, B.C);
				++At;
			}
			bSeam &= At > 0 && At == Single->GetFaces().Num();
		}
		const auto Prior = Mesh;
		Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
			*Worker, FIntVector(100, 0, 0), FIntVector(101, 1, 1), 2, 196608, Job.ToSharedRef(), Never, Mesh);
		bOutside = Result.Code == EVoxelLandM5ErrorCode::OutOfDomain && Mesh == Prior;
		Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Foreign.ToSharedRef(),
			*Worker, Min, Max, 2, 196608, Job.ToSharedRef(), Never, Mesh);
		bBudget = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Mesh == Prior;
		Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
			*Worker, Min, Max, 1, 196608, Job.ToSharedRef(), Never, Mesh);
		bLimit = Result.Code != EVoxelLandM5ErrorCode::None && Mesh == Prior;
		Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
			*Worker, Min, Max, 2, 196608, Job.ToSharedRef(), [] { return true; }, Mesh);
		bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Mesh == Prior;
		Async(EAsyncExecution::ThreadPool, [&] {
			auto Other = Mesh;
			const auto Wrong = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), Source.ToSharedRef(),
				*Worker, Min, Max, 2, 196608, Job.ToSharedRef(), Never, Other);
			bWorker = Wrong.Code == EVoxelLandM5ErrorCode::InvalidInput && Other == Prior;
		}).Wait();
	}).Wait();
	// A second captured graph uses X as its control input and a linear curve.
	// Its finite grid interpolates the control across the origin, producing a
	// real sloped height field; no synthetic chunk or certificate is involved.
	FVoxelLandM4ControlSnapshot RampControl; bool bRampControl = false, bRamp = false, bRampSeam = false;
	Async(EAsyncExecution::ThreadPool, [&] { bRampControl = MakeBaselineMeshControl(RampControl, Error, true); }).Wait();
	if (bRampControl)
	{
		ApplyBaselineMeshControl(*Config.Get(), Config->Continental, RampControl);
		Asset->Common.Continental = Config->Continental;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> RampSource, RampJob;
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> RampCapture;
		if (FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, RampSource)
			&& FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, RampJob)
			&& FVoxelLandM5BaseCapture::Capture(*Asset.Get(), RampSource.ToSharedRef(), RampCapture).Code == EVoxelLandM5ErrorCode::None && RampCapture)
		{
			Async(EAsyncExecution::ThreadPool, [&] {
				auto Snapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(RampCapture->GetSnapshot());
				auto Registry = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
				FVoxelLandM5Bounds Gate = Snapshot->Caves.Bounds;
				Gate.MinInclusive.X = Gate.MinInclusive.Y = Gate.MinInclusive.Z = -4000;
				Gate.MaxExclusive.X = Gate.MaxExclusive.Y = Gate.MaxExclusive.Z = 4000;
				FVoxelLandM5DefinitionInputs Inputs{Snapshot, nullptr, Registry, Gate, true, true};
				TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition;
				TSharedPtr<const FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Influence;
				TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe> Baseline;
				if (FVoxelLandM5DefinitionValidator::Validate(Inputs, RampSource.ToSharedRef(), Never, Definition).Code != EVoxelLandM5ErrorCode::None || !Definition
					|| FVoxelLandM5InfluenceSet::BuildFinite(Definition.ToSharedRef(), RampSource.ToSharedRef(), Never, Influence).Code != EVoxelLandM5ErrorCode::None || !Influence
					|| FVoxelLandM5CapturedFiniteScratch::BuildBound(RampCapture.ToSharedRef(), Definition.ToSharedRef(), Factory, Never, Baseline).Code != EVoxelLandM5ErrorCode::None || !Baseline) return;
				TUniquePtr<FVoxelLandWorkerContext> Worker;
				if (Baseline->GetGenerator().CreateWorker(Worker) || !Worker) return;
				TSharedPtr<const FVoxelLandM5BaselineMesh, ESPMode::ThreadSafe> Mesh;
				const auto Result = FVoxelLandM5BaselineMesh::Build(Baseline.ToSharedRef(), Influence.ToSharedRef(), RampSource.ToSharedRef(),
					*Worker, FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, 196608, RampJob.ToSharedRef(), Never, Mesh);
				if (Result.Code != EVoxelLandM5ErrorCode::None || !Mesh || Mesh->GetTriangles().IsEmpty()) return;
				int64 Low = MAX_int64, High = MIN_int64; bool Negative = false, Positive = false;
				for (int32 I = 0; I < Mesh->GetTriangles().Num(); ++I)
				{
					const auto& T = Mesh->GetTriangles()[I];
					Low = FMath::Min(Low, FMath::Min(T.A.Z, FMath::Min(T.B.Z, T.C.Z)));
					High = FMath::Max(High, FMath::Max(T.A.Z, FMath::Max(T.B.Z, T.C.Z)));
					Negative |= Mesh->GetFaces()[I].ChunkCoordinate.X == -1;
					Positive |= Mesh->GetFaces()[I].ChunkCoordinate.X == 0;
				}
				bRamp = Low < High && Negative && Positive;
				bRampSeam = HasSharedOwnerSeam(*Mesh);
			}).Wait();
		}
	}
	TestTrue(FString::Printf(TEXT("real ramp control: %s"), *Error), bRampControl);
	TestTrue(TEXT("real ramp emits varying heights across both owners"), bRamp);
	TestTrue(TEXT("ramp owners share exact seam vertices"), bRampSeam);
	TestTrue(FString::Printf(TEXT("real baseline mesh: %s"), *Failure), bBuilt);
	TestTrue(TEXT("flat real M4 plane"), bFlat);
	TestTrue(TEXT("exact owners and separate budgets"), bOwners);
	TestTrue(TEXT("requested owner rectangle retained"), bRange);
	TestTrue(TEXT("negative and positive chunk faces present"), bFaces);
	TestTrue(TEXT("independent owner meshes match across seam"), bSeam);
	TestTrue(TEXT("flat owners share exact seam vertices"), bSharedSeam);
	TestTrue(TEXT("uncovered chunk rejected atomically"), bOutside);
	TestTrue(TEXT("foreign source budget rejected atomically"), bBudget);
	TestTrue(TEXT("chunk limit rejected atomically"), bLimit);
	TestTrue(TEXT("cancellation preserves output"), bCancel);
	TestTrue(TEXT("foreign thread worker rejected atomically"), bWorker);
	return bRampControl && bRamp && bRampSeam && bBuilt && bFlat && bOwners && bRange && bFaces && bSeam && bSharedSeam && bOutside && bBudget && bLimit && bCancel && bWorker;
}
#endif
