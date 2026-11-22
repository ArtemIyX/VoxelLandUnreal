#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5FixedMesh.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FixedMeshTest, "VoxelLand.M5.Numeric.FixedMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FixedMeshTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024, 4096, Budget)))
	{
		return false;
	}
	FVoxelLandM5ValidationTriangle Triangle;
	// Voxel size 1 has snap step 2^-18 cm. Adjacent triangles can share
	// fractional negative global coordinates exactly without rounding to cm.
	Triangle.A = FVector3d(-1.25, 0.5, -1.0 / 262144.0);
	Triangle.B = FVector3d(0.0, 0.5, 0.0);
	Triangle.C = FVector3d(0.25, 1.0, 0.0);
	Triangle.SourceTriangle = 7;
	FVoxelLandM5FixedTriangleQ18 Result;
	const auto NeverCancel = [] {
		return false;
	};
	auto Status = VoxelLand::M5::FixedMesh::Convert(
		Triangle, 1, true, FVector3d(0.0, 0.25, 1.0 / 262144.0), Budget.ToSharedRef(), NeverCancel, Result);
	TestEqual(TEXT("fractional conversion"), int32(Status.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("negative fractional X"), Result.A.X, int64(-327680));
	TestEqual(TEXT("one negative Q18 unit"), Result.A.Z, int64(-1));
	TestEqual(TEXT("positive fractional X"), Result.C.X, int64(65536));
	TestEqual(TEXT("source index"), Result.SourceTriangle, 7);
	TestEqual(TEXT("zero renderer error"), Result.RendererErrorQ18.X, int64(0));
	TestTrue(TEXT("renderer Y bound is outward"), Result.RendererErrorQ18.Y >= int64(65536));
	TestTrue(TEXT("renderer Z bound is outward"), Result.RendererErrorQ18.Z >= int64(1));

	FVoxelLandM5ValidationTriangle Adjacent = Triangle;
	Adjacent.A = Triangle.B;
	Adjacent.B = Triangle.A;
	FVoxelLandM5FixedTriangleQ18 Other;
	Status = VoxelLand::M5::FixedMesh::Convert(
		Adjacent, 1, true, FVector3d::ZeroVector, Budget.ToSharedRef(), NeverCancel, Other);
	TestEqual(TEXT("adjacent conversion"), int32(Status.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("shared vertex X"), Other.A.X, Result.B.X);
	TestEqual(TEXT("shared vertex Y"), Other.A.Y, Result.B.Y);
	TestEqual(TEXT("shared vertex Z"), Other.A.Z, Result.B.Z);

	const int64 Sentinel = 1234567;
	const auto ExpectFailure = [this, &Budget, &NeverCancel, Sentinel](const TCHAR* Label,
								   const FVoxelLandM5ValidationTriangle& Input, int32 Size, bool bProvenance,
								   const FVector3d& Error) {
		FVoxelLandM5FixedTriangleQ18 Output;
		Output.A.X = Sentinel;
		const FVoxelLandM5Diagnostic Failure = VoxelLand::M5::FixedMesh::Convert(
			Input, Size, bProvenance, Error, Budget.ToSharedRef(), NeverCancel, Output);
		TestTrue(Label, Failure.Code != EVoxelLandM5ErrorCode::None);
		TestEqual(TEXT("failure preserves output"), Output.A.X, Sentinel);
	};
	ExpectFailure(TEXT("unproven provenance"), Triangle, 1, false, FVector3d::ZeroVector);
	FVoxelLandM5ValidationTriangle Bad = Triangle;
	Bad.A.X = 1.0 / 524288.0;
	ExpectFailure(TEXT("off finest grid"), Bad, 1, true, FVector3d::ZeroVector);
	Bad.A.X = 1.0 / 262144.0;
	ExpectFailure(TEXT("off coarser voxel grid"), Bad, 100, true, FVector3d::ZeroVector);
	Bad.A.X = 1048577.0;
	ExpectFailure(TEXT("out of domain"), Bad, 1, true, FVector3d::ZeroVector);
	Bad.A.X = std::numeric_limits<double>::quiet_NaN();
	ExpectFailure(TEXT("NaN"), Bad, 1, true, FVector3d::ZeroVector);
	// Construct the representations by bits so the fixtures retain nonzero
	// subnormals even when the host uses denormals-are-zero arithmetic.
	uint64 SubnormalBits = 1;
	FMemory::Memcpy(&Bad.A.X, &SubnormalBits, sizeof(SubnormalBits));
	ExpectFailure(TEXT("positive subnormal coordinate"), Bad, 1, true, FVector3d::ZeroVector);
	SubnormalBits = 0x8000000000000001ull;
	FMemory::Memcpy(&Bad.A.X, &SubnormalBits, sizeof(SubnormalBits));
	ExpectFailure(TEXT("negative subnormal coordinate"), Bad, 1, true, FVector3d::ZeroVector);
	ExpectFailure(
		TEXT("unknown renderer bound"), Triangle, 1, true, FVector3d(std::numeric_limits<double>::infinity(), 0, 0));
	ExpectFailure(TEXT("invalid voxel size"), Triangle, 0, true, FVector3d::ZeroVector);

	FVoxelLandM5FixedTriangleQ18 Cancelled;
	Cancelled.A.X = Sentinel;
	int32 Polls = 0;
	Status = VoxelLand::M5::FixedMesh::Convert(
		Triangle, 1, true, FVector3d::ZeroVector, Budget.ToSharedRef(), [&Polls] { return ++Polls == 4; }, Cancelled);
	TestEqual(TEXT("mid-conversion cancellation"), int32(Status.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancel preserves output"), Cancelled.A.X, Sentinel);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (TestTrue(TEXT("empty budget created"), FVoxelLandM5Budget::TryCreate(0, 0, EmptyBudget)))
	{
		Status = VoxelLand::M5::FixedMesh::Convert(
			Triangle, 1, true, FVector3d::ZeroVector, EmptyBudget.ToSharedRef(), NeverCancel, Cancelled);
		TestEqual(TEXT("work exhaustion"), int32(Status.Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
		TestEqual(TEXT("exhaustion preserves output"), Cancelled.A.X, Sentinel);
	}
	return true;
}
#endif
