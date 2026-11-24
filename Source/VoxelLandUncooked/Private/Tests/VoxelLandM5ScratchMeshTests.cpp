#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandChunkData.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchMesh.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"

// Controlled source exercises actual meshing; no candidate or world proof is implied.
class FVoxelLandM5ScratchMeshFixture final
{
public:
	static TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Make(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(1024 * 1024, Lease))
		{
			return nullptr;
		}
		TSharedPtr<FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Result =
			MakeShareable(new FVoxelLandM5ScratchChunks(MoveTemp(Lease)));
		Result->MinInclusive = FIntVector(-1, 0, 0);
		Result->MaxExclusive = FIntVector(1, 1, 1);
		Result->VoxelSizeCm = 100;
		for (int32 ChunkX = -1; ChunkX <= 0; ++ChunkX)
		{
			FVoxelLandChunkData& Chunk = Result->Chunks.AddDefaulted_GetRef();
			Chunk.Key.Coordinate = FIntVector(ChunkX, 0, 0);
			Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
			for (int32 Z = -1; Z <= 33; ++Z)
			{
				for (int32 Y = -1; Y <= 33; ++Y)
				{
					for (int32 X = -1; X <= 33; ++X)
					{
						Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 8 ? -100 : 100;
					}
				}
			}
		}
		// Deliberately reverse storage; produced face order must remain canonical.
		Swap(Result->Chunks[0], Result->Chunks[1]);
		return Result;
	}
	static void SetChecker(const FVoxelLandM5ScratchChunks& Source, bool bChecker)
	{
		FVoxelLandM5ScratchChunks& Mutable = const_cast<FVoxelLandM5ScratchChunks&>(Source);
		for (FVoxelLandChunkData& Chunk : Mutable.Chunks)
		{
			for (int32 Z = -1; Z <= 33; ++Z)
			{
				for (int32 Y = -1; Y <= 33; ++Y)
				{
					for (int32 X = -1; X <= 33; ++X)
					{
						const int32 Index = (X + 1) + 35 * ((Y + 1) + 35 * (Z + 1));
						Chunk.Density[Index] = bChecker ? (((X + Y + Z) & 1) ? -100 : 100)
							: (Z <= 8 ? -100 : 100);
					}
				}
			}
		}
	}
	static void ChangeSharedSample(const FVoxelLandM5ScratchChunks& Source)
	{
		FVoxelLandM5ScratchChunks& Mutable = const_cast<FVoxelLandM5ScratchChunks&>(Source);
		Mutable.Chunks[0].Density[0] ^= 1;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchMeshTest, "VoxelLand.M5.ScratchMesh.ActualSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchMeshTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bForeign = false, bLimit = false, bCancelled = false, bSeam = false, bRetained = false;
	bool bWork = false, bDense = false, bMidCancel = false, bAdmission = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Other;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Budget)
			|| !FVoxelLandM5Budget::TryCreate(1024ll * 1024 * 1024, 1000000000, Other))
		{
			return;
		}
		// Represent the retained capture metadata alongside the sparse mesh.
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Metadata;
		if (!Budget->TryReserveBytes(48ll * 1024 * 1024, Metadata))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Source =
			FVoxelLandM5ScratchMeshFixture::Make(Budget.ToSharedRef());
		if (!Source)
		{
			return;
		}
		const auto Never = [] { return false; };
		TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
		const FVoxelLandM5Diagnostic Built = FVoxelLandM5ScratchMesh::Build(Source.ToSharedRef(),
			2, 10000, Budget.ToSharedRef(), Never, Mesh);
		Failure = Built.Message;
		bSuccess = Built.Code == EVoxelLandM5ErrorCode::None && Mesh && Mesh->IsOwnedBy(*Budget)
			&& &Mesh->GetSource() == Source.Get() && Mesh->GetTriangles().Num() > 0;
		if (!bSuccess)
		{
			return;
		}
		// Mesh triangles on the analytic z=850 cm plane cross the negative to
		// positive X seam; all fixed coordinates must preserve that plane.
		bool bNegative = false, bPositive = false, bPlane = true;
		bool bFaces = Mesh->GetFaces().Num() == Mesh->GetTriangles().Num();
		FIntVector PreviousOwner(-2, 0, 0);
		int32 PreviousFace = -1;
		for (int32 Index = 0; bFaces && Index < Mesh->GetFaces().Num(); ++Index)
		{
			const FVoxelLandM5ChunkFaceSelection& Face = Mesh->GetFaces()[Index];
			const FVoxelLandM5FixedTriangleQ18& Triangle = Mesh->GetTriangles()[Index];
			bFaces &= (Face.ChunkCoordinate == FIntVector(-1, 0, 0) || Face.ChunkCoordinate == FIntVector(0, 0, 0))
				&& Face.FaceIndex == Triangle.SourceTriangle && Face.FaceIndex >= 0
				&& Face.ChunkCoordinate.X >= PreviousOwner.X;
			if (Face.ChunkCoordinate == PreviousOwner)
			{
				bFaces &= Face.FaceIndex > PreviousFace;
			}
			PreviousOwner = Face.ChunkCoordinate;
			PreviousFace = Face.FaceIndex;
		}
		for (const FVoxelLandM5FixedTriangleQ18& Triangle : Mesh->GetTriangles())
		{
			const FVoxelLandM5FixedPointQ18 Vertices[] = {Triangle.A, Triangle.B, Triangle.C};
			for (const FVoxelLandM5FixedPointQ18& Point : Vertices)
			{
				bPlane &= Point.Z == (int64(850) << 18);
				bNegative |= Point.X < 0;
				bPositive |= Point.X > 0;
			}
		}
		bSuccess &= bPlane && bNegative && bPositive && bFaces;
		int64 Live = 0, Peak = 0, Used = 0;
		Budget->GetUsage(Live, Peak, Used);
		bAdmission = Metadata.IsValid() && Peak <= 256ll * 1024 * 1024;
		const TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Previous = Mesh;
		const FVoxelLandM5Diagnostic Foreign = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 10000, Other.ToSharedRef(), Never, Mesh);
		bForeign = Foreign.Code == EVoxelLandM5ErrorCode::InvalidInput && Mesh == Previous;
		const FVoxelLandM5Diagnostic Limit = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 196609, Budget.ToSharedRef(), Never, Mesh);
		bLimit = Limit.Code == EVoxelLandM5ErrorCode::InvalidInput && Mesh == Previous;
		const FVoxelLandM5Diagnostic Cancel = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), [] { return true; }, Mesh);
		bCancelled = Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Mesh == Previous;
		FVoxelLandM5ScratchMeshFixture::ChangeSharedSample(*Source);
		const FVoxelLandM5Diagnostic Mismatch = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), Never, Mesh);
		bSeam = Mismatch.Code != EVoxelLandM5ErrorCode::None && Mesh == Previous;
		FVoxelLandM5ScratchMeshFixture::ChangeSharedSample(*Source);
		FVoxelLandM5ScratchMeshFixture::SetChecker(*Source, true);
		Budget->GetUsage(Live, Peak, Used);
		const int64 LiveBeforeDense = Live;
		const FVoxelLandM5Diagnostic Dense = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), Never, Mesh);
		Budget->GetUsage(Live, Peak, Used);
		bDense = Dense.Code == EVoxelLandM5ErrorCode::BudgetExceeded
			&& Dense.Message == TEXT("Scratch mesh triangle limit exhausted") && Mesh == Previous && Live == LiveBeforeDense;
		const int64 BeforeScan = Used;
		const auto CancelAfterPrecharge = [&Budget, BeforeScan] {
			int64 CurrentLive = 0, CurrentPeak = 0, CurrentWork = 0;
			Budget->GetUsage(CurrentLive, CurrentPeak, CurrentWork);
			return CurrentWork > BeforeScan + 24017;
		};
		const FVoxelLandM5Diagnostic DuringScan = FVoxelLandM5ScratchMesh::Build(
			Source.ToSharedRef(), 2, 196608, Budget.ToSharedRef(), CancelAfterPrecharge, Mesh);
		bMidCancel = DuringScan.Code == EVoxelLandM5ErrorCode::Cancelled && Mesh == Previous;
		FVoxelLandM5ScratchMeshFixture::SetChecker(*Source, false);
		TWeakPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Weak = Source;
		Source.Reset();
		bRetained = Weak.IsValid();
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
		if (!FVoxelLandM5Budget::TryCreate(2 * 1024 * 1024, 1, Tiny))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> TinySource =
			FVoxelLandM5ScratchMeshFixture::Make(Tiny.ToSharedRef());
		if (!TinySource)
		{
			return;
		}
		const FVoxelLandM5Diagnostic Work = FVoxelLandM5ScratchMesh::Build(
			TinySource.ToSharedRef(), 2, 10000, Tiny.ToSharedRef(), Never, Mesh);
		bWork = Work.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Mesh == Previous;
	}).Wait();
	TestTrue(FString::Printf(TEXT("actual mesh and seam: %s"), *Failure), bSuccess);
	TestTrue(TEXT("foreign budget rejected atomically"), bForeign);
	TestTrue(TEXT("triangle hard limit rejected atomically"), bLimit);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("mismatched shared sample rejected"), bSeam);
	TestTrue(TEXT("mesh retains source"), bRetained);
	TestTrue(TEXT("work exhaustion preserves output"), bWork);
	TestTrue(TEXT("sparse mesh and capture metadata fit 256 MiB"), bAdmission);
	TestTrue(TEXT("dense edge preflight rejects cap without allocating output"), bDense);
	TestTrue(TEXT("cancellation during preflight preserves output"), bMidCancel);
	return bSuccess;
}
#endif
