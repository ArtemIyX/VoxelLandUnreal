#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveBake.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedSupport.h"
#include "Generation/Caves/Geometry/VoxelLandCavePiecewiseSupport.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiChunkRoute.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchMesh.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "HAL/IConsoleManager.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Caves/Field/VoxelLandCaveBoundBase.h"
#include "World/Caves/Planning/VoxelLandCaveCapturedFiniteScratch.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"

class FVoxelLandM5RetainedPiecewiseFixture final
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
	static bool IsFlatPatchVertex(const FVoxelLandM5FixedPointQ18& P)
	{
		constexpr int64 Q = 262144;
		return P.X >= -450 * Q && P.X <= 450 * Q && P.Y >= 1150 * Q && P.Y <= 1750 * Q;
	}
	static bool IsKinkPatchVertex(const FVoxelLandM5FixedPointQ18& P)
	{
		constexpr int64 Q = 262144;
		return P.X >= -150 * Q && P.X <= 50 * Q && P.Y >= 1350 * Q && P.Y <= 1550 * Q;
	}
	static FVoxelLandM5Diagnostic CheckFlatRoute(const FVoxelLandM5ScratchMesh& Mesh, const FVoxelLandM5PointCm& Start,
		const FVoxelLandM5PointCm& End, int64 Margin, TConstArrayView<FVoxelLandM5ChunkFaceSelection> Faces,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> Cancel)
	{
		return VoxelLand::M5::MultiChunkRoute::Certify(
			Mesh, Start, End, 35, 185, Margin, 0, 1, 1, Faces, Budget, Cancel);
	}
	static bool MakeRetainedPiecewiseControl(FVoxelLandM4ControlSnapshot& Out, FString& Error, bool bKink = false)
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
		if (bKink)
		{
			Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::PositionX);
			Constant.Pins.RemoveAt(0);
		}
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
		if (bKink)
		{
			FVoxelLandM4CurveAuthoringKey Middle;
			First.OutputQ8 = Last.OutputQ8 = 3200 * 256;
			Middle.InputQ16 = 32768;
			Out.CurveKeys = {First, Middle, Last};
			Out.CookedCurve.SourceKeys = Out.CurveKeys;
			FVoxelLandM4CurveKey A, B, C;
			A.Output = C.Output = 3200 * 256;
			B.InputQ16 = 32768;
			C.InputQ16 = 65535;
			TArray<FVoxelLandM4CurveKey> Keys{A, B, C};
			TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
			if (!VoxelLandM4CurveBake::Bake(Keys, 0, 3200 * 256, Out.CookedCurve.SamplesQ8, Curve, Error))
			{
				return false;
			}
		}
		else
		{
			Out.CookedCurve.SamplesQ8.Init(0, 4097);
		}
		FCanonicalWriter Writer;
		for (int32 Sample : Out.CookedCurve.SamplesQ8)
		{
			Writer.U32(uint32(Sample));
		}
		const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
		FMemory::Memcpy(&Out.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		return true;
	}
	static void ApplyRetainedPiecewiseControl(
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

struct FVoxelLandM5RetainedControlTask final
{
	FVoxelLandM4ControlSnapshot& Control;
	FString& Error;
	bool& bResult;
	bool bKink;
	void operator()() const
	{
		bResult = FVoxelLandM5RetainedPiecewiseFixture::MakeRetainedPiecewiseControl(Control, Error, bKink);
	}
};
struct FVoxelLandM5RetainedFlatTask final
{
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& Capture;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Budget;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& Foreign;
	TFunctionRef<bool()> Never;
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		Factory;
	bool& bMesh;
	bool& bFaces;
	bool& bFlat;
	bool& bSeam;
	bool& bSmallMargin;
	bool& bMissing;
	bool& bForeign;
	bool& bCancel;
	bool& bObstacle;
	bool& bLargeMargin;
	bool& bBadFace;
	FString& Failure;
	void operator()() const
	{
		TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
		FVoxelLandM5Diagnostic Result =
			FVoxelLandM5CapturedFiniteScratch::Build(Capture.ToSharedRef(), Factory, Never, Scratch);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Scratch)
		{
			Failure = Result.Message;
			return;
		}
		const TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator =
			Scratch->GetScratch().GetGeneratorShared();
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Generator->CreateWorker(Worker) || !Worker)
		{
			return;
		}
		FVoxelLandM5PointCm Min;
		Min.X = -37;
		Min.Y = Min.Z = -5;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		Result = FVoxelLandM5BaselineWindow::BuildBox(
			Generator, *Worker, Min, FIntVector(75, 43, 43), Budget.ToSharedRef(), Never, Window);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Window)
		{
			Failure = Result.Message;
			return;
		}
		const FVoxelLandM5WorldSnapshot& Snapshot = Capture->GetSnapshot();
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		Result = FVoxelLandM5ScratchChunks::Build(Snapshot.Caves, Snapshot.Manifest, Snapshot.Base.Seed,
			Window.ToSharedRef(), TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>(), nullptr,
			FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, Budget.ToSharedRef(), Never, Chunks);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Chunks)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
		Result = FVoxelLandM5ScratchMesh::Build(Chunks.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), Never, Mesh);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Mesh)
		{
			Failure = Result.Message;
			return;
		}
		bMesh = Mesh->GetTriangles().Num() > 0 && Mesh->GetTriangles().Num() == Mesh->GetFaces().Num();
		TArray<FVoxelLandM5ChunkFaceSelection> Faces;
		bool bLeft = false, bRight = false;
		bFlat = true;
		constexpr int64 Q = 262144;
		for (int32 I = 0; I < Mesh->GetTriangles().Num(); ++I)
		{
			const FVoxelLandM5FixedTriangleQ18& T = Mesh->GetTriangles()[I];
			if (FVoxelLandM5RetainedPiecewiseFixture::IsFlatPatchVertex(T.A)
				&& FVoxelLandM5RetainedPiecewiseFixture::IsFlatPatchVertex(T.B)
				&& FVoxelLandM5RetainedPiecewiseFixture::IsFlatPatchVertex(T.C))
			{
				bFlat &= T.A.Z == 1050 * Q && T.B.Z == 1050 * Q && T.C.Z == 1050 * Q;
				const FVoxelLandM5ChunkFaceSelection& Face = Mesh->GetFaces()[I];
				bLeft |= Face.ChunkCoordinate.X == -1;
				bRight |= Face.ChunkCoordinate.X == 0;
				Faces.Add(Face);
			}
		}
		bFaces = bLeft && bRight && Faces.Num() > 0 && Faces.Num() <= 256;
		if (!bFaces || !bFlat)
		{
			return;
		}
		FVoxelLandM5PointCm Start, End;
		Start.X = -150;
		End.X = 150;
		Start.Y = End.Y = 1450;
		Start.Z = End.Z = 1050;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 0, Faces, Budget.ToSharedRef(), Never);
		Failure = Result.Message;
		bSeam = Result.Code == EVoxelLandM5ErrorCode::None;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 10, Faces, Budget.ToSharedRef(), Never);
		bSmallMargin = Result.Code == EVoxelLandM5ErrorCode::None;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 500, Faces, Budget.ToSharedRef(), Never);
		bLargeMargin = Result.Code == EVoxelLandM5ErrorCode::Unproven;
		FVoxelLandM5PointCm Outside = Start;
		Outside.X = -3100;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Outside, End, 0, Faces, Budget.ToSharedRef(), Never);
		bMissing = Result.Code == EVoxelLandM5ErrorCode::Unproven
				&& Result.Message == TEXT("Sweep requires omitted neighboring mesh owners");
		TArray<FVoxelLandM5ChunkFaceSelection> BadFaces = Faces;
		BadFaces[0].FaceIndex = MAX_int32;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 0, BadFaces, Budget.ToSharedRef(), Never);
		bBadFace = Result.Code == EVoxelLandM5ErrorCode::InvalidInput;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 0, Faces, Foreign.ToSharedRef(), Never);
		bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput;
		Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
			*Mesh, Start, End, 0, Faces, Budget.ToSharedRef(), FVoxelLandM5RetainedPiecewiseFixture::AlwaysCancel);
		bCancel = Result.Code == EVoxelLandM5ErrorCode::Cancelled;
		// Remove one face at the seam, leaving it in the retained obstacle mesh.
		for (int32 Index = 0; Index < Faces.Num(); ++Index)
		{
			TArray<FVoxelLandM5ChunkFaceSelection> Partial = Faces;
			Partial.RemoveAt(Index);
			Result = FVoxelLandM5RetainedPiecewiseFixture::CheckFlatRoute(
				*Mesh, Start, End, 10, Partial, Budget.ToSharedRef(), Never);
			if (Result.Code == EVoxelLandM5ErrorCode::Unproven && Result.Stage == TEXT("M5.SweptTriangle"))
			{
				bObstacle = true;
				break;
			}
		}
	}
};
struct FVoxelLandM5RetainedKinkTask final
{
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& KinkCapture;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe>& KinkBudget;
	TFunctionRef<bool()> Never;
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		Factory;
	FString& Failure;
	bool& bKinkMesh;
	bool& bKinkPlanes;
	bool& bRepresentable;
	bool& bFallback;
	bool& bKinkMargin;
	bool& bKinkObstacle;
	void FindObstacle(const FVoxelLandM5ScratchMesh& MeshValue, const FVoxelLandM5PointCm& A,
		const FVoxelLandM5PointCm& B, int32 Skin, const TArray<FVoxelLandM5ChunkFaceSelection>& Faces) const
	{
		const FVoxelLandM5ScratchMesh* Mesh = &MeshValue;
		for (int32 I = 0; I < Faces.Num(); ++I)
		{
			TArray<FVoxelLandM5ChunkFaceSelection> Partial = Faces;
			Partial.RemoveAt(I);
			const FVoxelLandM5Diagnostic Obstacle = VoxelLand::M5::MultiChunkRoute::Certify(
				*Mesh, A, B, 10, 20, 1, Skin, 1, 1, Partial, KinkBudget.ToSharedRef(), Never);
			if (Obstacle.Code == EVoxelLandM5ErrorCode::Unproven && Obstacle.Stage == TEXT("M5.SweptTriangle"))
			{
				bKinkObstacle = true;
				break;
			}
		}
	}
	void TryCandidate(const FVoxelLandM5ScratchMesh& MeshValue, const TArray<FVoxelLandM5FixedTriangleQ18>& Selected,
		const TArray<FVoxelLandM5ChunkFaceSelection>& Faces, int64 Z, int32 Skin) const
	{
		constexpr int64 Q = 262144;
		FVoxelLandM5Diagnostic Result;
		const FVoxelLandM5ScratchMesh* Mesh = &MeshValue;

		FVoxelLandM5PointCm A, B;
		A.X = -60;
		B.X = -40;
		A.Y = B.Y = 1450;
		A.Z = B.Z = Z;
		FVoxelLandM5FixedPointQ18 FA, FB;
		FA.X = A.X * Q;
		FA.Y = A.Y * Q;
		FA.Z = A.Z * Q;
		FB.X = B.X * Q;
		FB.Y = B.Y * Q;
		FB.Z = B.Z * Q;
		const FVoxelLandM5Diagnostic Flat = VoxelLand::M5::FixedSupport::CertifyLinearContactWithMargin(
			FA, FB, 10 * Q, 20 * Q, Skin * Q, 0, 1, 1, Selected, KinkBudget.ToSharedRef(), Never);
		if (Flat.Code != EVoxelLandM5ErrorCode::Unproven)
		{
			return;
		}
		const FVoxelLandM5Diagnostic Piece = VoxelLand::M5::PiecewiseSupport::CertifyLinearContactWithMargin(
			FA, FB, 10 * Q, 20 * Q, Skin * Q, 0, 1, 1, Selected, KinkBudget.ToSharedRef(), Never);
		if (Piece.Code != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		Result = VoxelLand::M5::MultiChunkRoute::Certify(
			*Mesh, A, B, 10, 20, 0, Skin, 1, 1, Faces, KinkBudget.ToSharedRef(), Never);
		if (Result.Code != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		bFallback = true;
		const FVoxelLandM5Diagnostic PieceMargin = VoxelLand::M5::PiecewiseSupport::CertifyLinearContactWithMargin(
			FA, FB, 10 * Q, 20 * Q, Skin * Q, Q, 1, 1, Selected, KinkBudget.ToSharedRef(), Never);
		if (PieceMargin.Code != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		Result = VoxelLand::M5::MultiChunkRoute::Certify(
			*Mesh, A, B, 10, 20, 1, Skin, 1, 1, Faces, KinkBudget.ToSharedRef(), Never);
		if (Result.Code != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		bKinkMargin = true;
		FindObstacle(*Mesh, A, B, Skin, Faces);
	}
	void Search(const FVoxelLandM5ScratchMesh& MeshValue, const TArray<FVoxelLandM5FixedTriangleQ18>& Selected,
		const TArray<FVoxelLandM5ChunkFaceSelection>& Faces, int64 LevelHeightCm) const
	{
		for (int64 Z = LevelHeightCm + 6; Z <= LevelHeightCm + 20 && !bKinkObstacle; ++Z)
		{
			for (int32 Skin = 0; Skin <= 10 && !bKinkObstacle; ++Skin)
			{
				TryCandidate(MeshValue, Selected, Faces, Z, Skin);
			}
		}
	}

	void operator()() const
	{
		TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Scratch;
		FVoxelLandM5Diagnostic Result =
			FVoxelLandM5CapturedFiniteScratch::Build(KinkCapture.ToSharedRef(), Factory, Never, Scratch);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Scratch)
		{
			Failure = Result.Message;
			return;
		}
		const TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator =
			Scratch->GetScratch().GetGeneratorShared();
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Generator->CreateWorker(Worker) || !Worker)
		{
			return;
		}
		FVoxelLandM5PointCm Min;
		Min.X = -37;
		Min.Y = Min.Z = -5;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		Result = FVoxelLandM5BaselineWindow::BuildBox(
			Generator, *Worker, Min, FIntVector(75, 43, 43), KinkBudget.ToSharedRef(), Never, Window);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Window)
		{
			Failure = Result.Message;
			return;
		}
		const FVoxelLandM5WorldSnapshot& Snapshot = KinkCapture->GetSnapshot();
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		Result = FVoxelLandM5ScratchChunks::Build(Snapshot.Caves, Snapshot.Manifest, Snapshot.Base.Seed,
			Window.ToSharedRef(), TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>(), nullptr,
			FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 2, KinkBudget.ToSharedRef(), Never, Chunks);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Chunks)
		{
			Failure = Result.Message;
			return;
		}
		TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
		Result = FVoxelLandM5ScratchMesh::Build(Chunks.ToSharedRef(), 2, 10000, KinkBudget.ToSharedRef(), Never, Mesh);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Mesh)
		{
			Failure = Result.Message;
			return;
		}
		constexpr int64 Q = 262144;
		TArray<FVoxelLandM5FixedTriangleQ18> Selected;
		TArray<FVoxelLandM5ChunkFaceSelection> Faces;
		bool bSloped = false, bLevel = false, bLeftOwner = false, bRightOwner = false;
		int64 LevelHeightQ18 = 0;
		for (int32 I = 0; I < Mesh->GetTriangles().Num(); ++I)
		{
			const FVoxelLandM5FixedTriangleQ18& T = Mesh->GetTriangles()[I];
			if (!FVoxelLandM5RetainedPiecewiseFixture::IsKinkPatchVertex(T.A)
				|| !FVoxelLandM5RetainedPiecewiseFixture::IsKinkPatchVertex(T.B)
				|| !FVoxelLandM5RetainedPiecewiseFixture::IsKinkPatchVertex(T.C))
			{
				continue;
			}
			Selected.Add(T);
			Faces.Add(Mesh->GetFaces()[I]);
			bLeftOwner |= Mesh->GetFaces()[I].ChunkCoordinate.X == -1;
			bRightOwner |= Mesh->GetFaces()[I].ChunkCoordinate.X == 0;
			const FVector3d U(double(T.B.X - T.A.X), double(T.B.Y - T.A.Y), double(T.B.Z - T.A.Z));
			const FVector3d V(double(T.C.X - T.A.X), double(T.C.Y - T.A.Y), double(T.C.Z - T.A.Z));
			const FVector3d N = FVector3d::CrossProduct(U, V);
			bSloped |= N.X != 0 && N.Z != 0;
			if (N.X == 0 && N.Y == 0 && N.Z != 0)
			{
				bLevel = true;
				LevelHeightQ18 = T.A.Z;
			}
		}
		bKinkMesh = Selected.Num() > 0 && Selected.Num() <= 256 && bLeftOwner && bRightOwner;
		bKinkPlanes = bKinkMesh && bSloped && bLevel;
		if (!bKinkPlanes)
		{
			return;
		}
		const FVoxelLandM5FixedPointQ18 Origin = Selected[0].A;
		bRepresentable = Origin.X % Q == 0 && Origin.Y % Q == 0 && Origin.Z % Q == 0;
		for (const FVoxelLandM5FixedTriangleQ18& T : Selected)
		{
			const FVoxelLandM5FixedPointQ18 Points[] = {T.A, T.B, T.C};
			for (const FVoxelLandM5FixedPointQ18& P : Points)
			{
				const int64 DX = P.X - Origin.X, DY = P.Y - Origin.Y, DZ = P.Z - Origin.Z;
				bRepresentable &= DX % Q == 0 && DY % Q == 0 && DZ % Q == 0 && FMath::Abs(DX / Q) <= 256
							   && FMath::Abs(DY / Q) <= 256 && FMath::Abs(DZ / Q) <= 256;
			}
		}
		if (!bRepresentable)
		{
			return;
		}
		if (LevelHeightQ18 % Q != 0)
		{
			return;
		}
		const int64 LevelHeightCm = LevelHeightQ18 / Q;
		Search(*Mesh, Selected, Faces, LevelHeightCm);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RetainedPiecewiseRouteTest, "VoxelLand.M5.RetainedPiecewiseRoute.RealM4",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5RetainedPiecewiseRouteTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("capture on game thread"), IsInGameThread()))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot Control;
	FString Error;
	bool bControl = false;
	Async(EAsyncExecution::ThreadPool, FVoxelLandM5RetainedControlTask{Control, Error, bControl, false}).Wait();
	if (!TestTrue(FString::Printf(TEXT("control: %s"), *Error), bControl))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandM4WorldConfig> Config(NewObject<UVoxelLandM4WorldConfig>());
	FVoxelLandM5RetainedPiecewiseFixture::ApplyRetainedPiecewiseControl(*Config.Get(), Config->Continental, Control);
	FVoxelLandM5RetainedPiecewiseFixture::ApplyRetainedPiecewiseControl(*Config.Get(), Config->Mountain, Control);
	FVoxelLandM5RetainedPiecewiseFixture::ApplyRetainedPiecewiseControl(*Config.Get(), Config->Peaks, Control);
	TStrongObjectPtr<UVoxelLandFiniteWorld> Asset(NewObject<UVoxelLandFiniteWorld>());
	Asset->Common.Continental = Config->Continental;
	Asset->Common.Mountain = Config->Mountain;
	Asset->Common.Peaks = Config->Peaks;
	Asset->Common.Seed = 12345;
	Asset->Common.BaseHeightQ8 = 1050 * 256;
	Asset->Common.Caves.StageId = FGuid(11, 22, 33, 44);
	Asset->Common.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
	Asset->Common.Caves.RoofThicknessCm = 0;
	Asset->Common.Caves.Pockets.bEnabled = true;
	Asset->Common.Caves.Pockets.AmplitudeCm = 0;
	Asset->Common.Caves.Limits.MaxValidationWork = 1000000000;
	Asset->Finite.bGlobalErosion = false;
	Asset->Finite.CellsX = Asset->Finite.CellsY = 4;
	Asset->Finite.OriginXCm = Asset->Finite.OriginYCm = -12800;
	Asset->Finite.GridSpacingCm = 6400;
	IConsoleVariable* ProcessBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.M4.PrepassBudgetMiB"));
	if (!TestNotNull(TEXT("M4 process budget"), ProcessBudget) || ProcessBudget->GetInt() != 256)
	{
		AddError(TEXT("Retained route fixture requires the M4 process budget to be 256 MiB"));
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Foreign;
	if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Budget)
		|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Foreign))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	const FVoxelLandM5Diagnostic Captured =
		FVoxelLandM5BaseCapture::Capture(*Asset.Get(), Budget.ToSharedRef(), Capture);
	if (!TestTrue(FString::Printf(TEXT("capture: %s"), *Captured.Message),
			Captured.Code == EVoxelLandM5ErrorCode::None && Capture))
	{
		return false;
	}
	const TFunction<bool()> Never = FVoxelLandM5RetainedPiecewiseFixture::NeverCancel;
	const TFunction<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
		TUniquePtr<FVoxelLandWorkerContext>&)>
		Factory = FVoxelLandM5RetainedPiecewiseFixture::CreateWorker;
	bool bMesh = false, bFaces = false, bFlat = false, bSeam = false, bSmallMargin = false;
	bool bMissing = false, bForeign = false, bCancel = false, bObstacle = false, bLargeMargin = false, bBadFace = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool,
		FVoxelLandM5RetainedFlatTask{Capture, Budget, Foreign, Never, Factory, bMesh, bFaces, bFlat, bSeam,
			bSmallMargin, bMissing, bForeign, bCancel, bObstacle, bLargeMargin, bBadFace, Failure})
		.Wait();
	// Generate a second real M4 field with a V in its finite height grid.
	// Search only integer feet/skin; neither triangles nor face identities are
	// manufactured. This also independently checks that fallback is required.
	Capture.Reset();
	FVoxelLandM4ControlSnapshot KinkControl;
	bool bKinkControl = false, bKinkMesh = false, bKinkPlanes = false, bFallback = false;
	bool bKinkMargin = false, bKinkObstacle = false, bRepresentable = false;
	Async(EAsyncExecution::ThreadPool, FVoxelLandM5RetainedControlTask{KinkControl, Error, bKinkControl, true}).Wait();
	if (bKinkControl)
	{
		FVoxelLandM5RetainedPiecewiseFixture::ApplyRetainedPiecewiseControl(
			*Config.Get(), Config->Continental, KinkControl);
		Asset->Common.Continental = Config->Continental;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> KinkBudget;
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> KinkCapture;
		if (FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, KinkBudget)
			&& FVoxelLandM5BaseCapture::Capture(*Asset.Get(), KinkBudget.ToSharedRef(), KinkCapture).Code
				   == EVoxelLandM5ErrorCode::None
			&& KinkCapture)
		{
			Async(EAsyncExecution::ThreadPool,
				FVoxelLandM5RetainedKinkTask{KinkCapture, KinkBudget, Never, Factory, Failure, bKinkMesh, bKinkPlanes,
					bRepresentable, bFallback, bKinkMargin, bKinkObstacle})
				.Wait();
		}
	}
	TestTrue(FString::Printf(TEXT("real kink control: %s"), *Error), bKinkControl);
	TestTrue(TEXT("real kink faces span owners"), bKinkMesh);
	TestTrue(TEXT("real selected faces contain sloped and level planes"), bKinkPlanes);
	TestTrue(TEXT("selected real mesh fits exact local representation"), bRepresentable);
	TestTrue(TEXT("real retained mesh needs and passes piecewise fallback"), bFallback);
	TestTrue(TEXT("real kink retains positive-margin piecewise support"), bKinkMargin);
	TestTrue(TEXT("unselected real kink face fails swept obstacle check"), bKinkObstacle);
	TestTrue(FString::Printf(TEXT("actual composed CPU mesh: %s"), *Failure), bMesh);
	TestTrue(TEXT("selected real faces span seam"), bFaces);
	TestTrue(TEXT("selected M4 plane is exact"), bFlat);
	TestTrue(TEXT("retained flat route crosses seam"), bSeam);
	TestTrue(TEXT("small clearance margin succeeds"), bSmallMargin);
	TestTrue(TEXT("large clearance margin fails closed"), bLargeMargin);
	TestTrue(TEXT("missing owner fails closed"), bMissing);
	TestTrue(TEXT("unknown retained face rejected"), bBadFace);
	TestTrue(TEXT("foreign mesh budget rejected"), bForeign);
	TestTrue(TEXT("cancellation propagated"), bCancel);
	TestTrue(TEXT("unselected retained face remains obstruction"), bObstacle);
	return bKinkControl && bKinkMesh && bKinkPlanes && bRepresentable && bFallback && bKinkMargin && bKinkObstacle
		&& bMesh && bFaces && bFlat && bSeam && bSmallMargin && bLargeMargin && bMissing && bBadFace && bForeign
		&& bCancel && bObstacle;
}
#endif
