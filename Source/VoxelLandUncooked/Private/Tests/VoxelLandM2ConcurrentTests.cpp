#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM1Fixtures.h"
#include "Generation/VoxelLandMeshing.h"
#include "Data/Structs/VoxelLandM2MeshRun.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"

namespace
{
using VoxelLandM2TestData::FMeshRun;
FMeshRun RunMesh(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandChunkKey Key)
{
	FMeshRun Out;
	const auto Field = VoxelLandM1Fixtures::GenerateShared(Generator, Key);
	if (Field.Error || !Field.Chunk)
	{
		return Out;
	}
	FVoxelLandMeshingScratch Scratch;
	Out.DensityHash = Field.Chunk->Hash;
	Out.bSuccess = VoxelLandMeshing::BuildSurfaceNets(
					   *Field.Chunk, Generator->GetProfile().VoxelSizeCm, Scratch, Out.Mesh, [] { return false; })
				== EVoxelLandMeshResult::Success;
	return Out;
}
bool SameMesh(const FMeshRun& A, const FMeshRun& B)
{
	if (!A.bSuccess || !B.bSuccess || A.DensityHash != B.DensityHash || A.Mesh.Indices != B.Mesh.Indices
		|| A.Mesh.Vertices.Num() != B.Mesh.Vertices.Num())
	{
		return false;
	}
	for (int32 I = 0; I < A.Mesh.Vertices.Num(); ++I)
	{
		const auto& X = A.Mesh.Vertices[I];
		const auto& Y = B.Mesh.Vertices[I];
		if (X.Position != Y.Position || X.Normal != Y.Normal || X.Tangent != Y.Tangent || X.UV != Y.UV
			|| X.Color != Y.Color)
		{
			return false;
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2ConcurrentTest, "VoxelLand.M2.ConcurrentDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM2ConcurrentTest::RunTest(const FString& Parameters)
{
	const auto Fixture = Async(EAsyncExecution::ThreadPool, [] {
		return VoxelLandM1Fixtures::Generate(VoxelLandM1Fixtures::Noise(EVoxelLandOpcode::Fbm3),
			FVoxelLandZoneProfile(), FVoxelLandChunkKey(), FVoxelLandBackendOptions(), 54321);
	}).Get();
	if (Fixture.Error || !Fixture.Generator)
	{
		AddError(TEXT("Failed to create noise generator"));
		return false;
	}
	TArray<FVoxelLandChunkKey> Keys;
	for (int32 X = -2; X <= 1; ++X)
	{
		FVoxelLandChunkKey Key;
		Key.Coordinate.X = X;
		Keys.Add(Key);
	}
	TArray<FMeshRun> Serial;
	for (const auto Key : Keys)
	{
		Serial.Add(Async(EAsyncExecution::ThreadPool, [Generator = Fixture.Generator, Key] {
			return RunMesh(Generator, Key);
		}).Get());
	}
	TArray<TFuture<FMeshRun>> Pending;
	for (int32 I = Keys.Num() - 1; I >= 0; --I)
	{
		Pending.Add(Async(EAsyncExecution::ThreadPool,
			[Generator = Fixture.Generator, Key = Keys[I]] { return RunMesh(Generator, Key); }));
	}
	for (int32 I = 0; I < Pending.Num(); ++I)
	{
		TestTrue(
			TEXT("shuffled concurrent mesh equals serial"), SameMesh(Serial[Keys.Num() - 1 - I], Pending[I].Get()));
	}
	const bool bCancel = Async(EAsyncExecution::ThreadPool, [Generator = Fixture.Generator] {
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (Generator->CreateWorker(Worker))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		const FVoxelLandError Error =
			Generator->GenerateChunkCancellable(FVoxelLandChunkKey(), *Worker, Chunk, [] { return true; });
		return Error.Code == EVoxelLandErrorCode::Cancelled && !Chunk;
	}).Get();
	TestTrue(TEXT("cancelled generation publishes no field"), bCancel);
	return true;
}
#endif
