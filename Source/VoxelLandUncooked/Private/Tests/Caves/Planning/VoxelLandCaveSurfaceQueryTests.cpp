#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveSurfaceQuery.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SurfaceQueryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FlatSurfaceQueryTest, "VoxelLand.M5.SurfaceQuery.FlatAndFailures", M5SurfaceQueryFlags)
bool FVoxelLandM5FlatSurfaceQueryTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::SurfaceQuery;
	const FIntVector Dims(9, 9, 4);
	FVoxelLandM5PointCm Origin;
	Origin.X = -400;
	Origin.Y = -400;
	Origin.Z = -200;
	TArray<int16> Samples;
	Samples.Init(-10, 9 * 9 * 4);
	for (int32 Index = 9 * 9 * 2; Index < Samples.Num(); ++Index)
	{
		Samples[Index] = 10;
	}
	TArray<uint8> Protection;
	Protection.Init(0, 9 * 9);
	int64 Height = 123, MinHeight = 124, MaxHeight = 125;
	int32 Slope = 126;
	bool bProtected = true, bFlooded = false;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create work budget"), FVoxelLandM5Budget::TryCreate(0, 100000, Budget)))
	{
		return false;
	}
	const auto NeverCancel = []() {
		return false;
	};
	const auto Query = [&](int64 X, int64 Y, int32 Radius, int64 MaxSamples, TFunctionRef<bool()> IsCancelled) {
		return QueryFlatSurface(Budget.ToSharedRef(), Dims, Origin, 100, Samples, Protection, X, Y, Radius, true, -50,
			MaxSamples, IsCancelled, Height, MinHeight, MaxHeight, Slope, bProtected, bFlooded);
	};
	auto Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("flat midpoint accepted"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("height is exact negative midpoint"), Height, int64(-50));
	TestEqual(TEXT("minimum height"), MinHeight, int64(-50));
	TestEqual(TEXT("maximum height"), MaxHeight, int64(-50));
	TestEqual(TEXT("flat slope"), Slope, 0);
	TestFalse(TEXT("unprotected footprint"), bProtected);
	TestTrue(TEXT("water at surface floods"), bFlooded);
	Protection[1 + 9] = 1;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("halo protection valid"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	TestTrue(TEXT("halo protection conservatively propagated"), bProtected);
	Protection[1 + 9] = 0;

	Height = 123;
	Samples[3 + 9 * 3 + 81 * 3] = -2; // A second crossing in a single column.
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("overhang rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("failure leaves height"), Height, int64(123));
	Samples[3 + 9 * 3 + 81 * 3] = 10;
	Samples[3 + 9 * 3 + 81 * 2] = 0;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("zero boundary rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	Samples[3 + 9 * 3 + 81 * 2] = 9;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("varying crossing rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	Samples[3 + 9 * 3 + 81 * 2] = 10;
	Result = Query(-400, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("missing halo rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::OutOfDomain));
	Result = Query(-100, -100, 0, 143, NeverCancel);
	TestEqual(TEXT("work cap rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	int32 Calls = 0;
	const auto CancelLater = [&Calls]() {
		return ++Calls == 3;
	};
	Result = Query(-100, -100, 0, 144, CancelLater);
	TestEqual(TEXT("mid scan cancellation"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancel leaves height"), Height, int64(123));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TightBudget;
	if (!TestTrue(TEXT("create exact budget"), FVoxelLandM5Budget::TryCreate(0, 253, TightBudget)))
	{
		return false;
	}
	Result = QueryFlatSurface(TightBudget.ToSharedRef(), Dims, Origin, 100, Samples, Protection, -100, -100, 0, true,
		-50, 144, NeverCancel, Height, MinHeight, MaxHeight, Slope, bProtected, bFlooded);
	TestEqual(TEXT("exact shared charge succeeds"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	Height = 123;
	Result = QueryFlatSurface(TightBudget.ToSharedRef(), Dims, Origin, 100, Samples, Protection, -100, -100, 0, true,
		-50, 144, NeverCancel, Height, MinHeight, MaxHeight, Slope, bProtected, bFlooded);
	TestEqual(
		TEXT("cumulative shared budget enforced"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("shared budget failure leaves height"), Height, int64(123));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5VariableFlatSurfaceTest, "VoxelLand.M5.SurfaceQuery.VariableFlatMesh", M5SurfaceQueryFlags)
bool FVoxelLandM5VariableFlatSurfaceTest::RunTest(const FString& Parameters)
{
	FVoxelLandChunkData Chunk;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				const int16 Magnitude = static_cast<int16>(1 + ((X + 1) * 37 + (Y + 1) * 19) % 1000);
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 0 ? -Magnitude : Magnitude;
			}
		}
	}
	TArray<uint8> Protection;
	Protection.Init(0, 35 * 35);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create work budget"), FVoxelLandM5Budget::TryCreate(0, 10000, Budget)))
	{
		return false;
	}
	FVoxelLandM5PointCm Origin;
	Origin.X = -100;
	Origin.Y = -100;
	Origin.Z = -100;
	int64 Height = 0, MinHeight = 0, MaxHeight = 0;
	int32 Slope = -1;
	bool bProtected = true, bFlooded = true;
	const auto NeverCancel = []() {
		return false;
	};
	const FVoxelLandM5Diagnostic Result = VoxelLand::M5::SurfaceQuery::QueryFlatSurface(Budget.ToSharedRef(),
		FIntVector(35, 35, 35), Origin, 100, Chunk.Density, Protection, 1000, 1000, 0, false, 0, 1260, NeverCancel,
		Height, MinHeight, MaxHeight, Slope, bProtected, bFlooded);
	TestEqual(TEXT("variable symmetric crossings accepted"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("analytic midpoint"), Height, int64(50));
	TestEqual(TEXT("minimum midpoint"), MinHeight, Height);
	TestEqual(TEXT("maximum midpoint"), MaxHeight, Height);
	TestEqual(TEXT("zero slope"), Slope, 0);
	TestFalse(TEXT("no protection"), bProtected);
	TestFalse(TEXT("no water"), bFlooded);
	FVoxelLandMeshingScratch Scratch;
	FVoxelLandMeshData Mesh;
	if (!TestEqual(TEXT("production mesh succeeds"),
			int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, NeverCancel)),
			int32(EVoxelLandMeshResult::Success)))
	{
		return false;
	}
	TestTrue(TEXT("mesh has triangles"), Mesh.Indices.Num() > 0);
	for (const FVoxelLandMeshVertex& Vertex : Mesh.Vertices)
	{
		TestEqual(TEXT("mesh vertex matches queried midpoint"), Vertex.Position.Z, static_cast<float>(Height));
	}
	return true;
}
#endif
