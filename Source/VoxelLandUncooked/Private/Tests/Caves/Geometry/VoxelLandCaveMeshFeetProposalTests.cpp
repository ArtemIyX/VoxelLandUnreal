#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMeshFeetProposal.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5MeshFeetProposalFixture final
{
public:
	static FVoxelLandM5FixedTriangleQ18 Triangle(double A, double B, double C)
	{
		FVoxelLandM5FixedTriangleQ18 Result;
		constexpr int64 Q = int64(1) << 18;
		Result.A.X = -10 * Q;
		Result.A.Y = -10 * Q;
		Result.A.Z = int64(A * double(Q));
		Result.B.X = 10 * Q;
		Result.B.Y = -10 * Q;
		Result.B.Z = int64(B * double(Q));
		Result.C.X = 0;
		Result.C.Y = 10 * Q;
		Result.C.Z = int64(C * double(Q));
		return Result;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeshFeetProposalTest, "VoxelLand.M5.MeshFeetProposal.Analytic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeshFeetProposalTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(1000000, 10000000, Budget)))
	{
		return false;
	}
	const auto Never = [] {
		return false;
	};
	FVoxelLandM5PointCm Center;
	Center.Z = 10;
	FVoxelLandM5PointCm Out;
	Out.X = 99;
	Out.Y = 98;
	Out.Z = 97;
	TArray<FVoxelLandM5FixedTriangleQ18> Triangles;
	Triangles.Add(FVoxelLandM5MeshFeetProposalFixture::Triangle(-4.0, -4.0, -4.0));
	Triangles.Add(FVoxelLandM5MeshFeetProposalFixture::Triangle(0.25, 0.75, 0.25));
	FVoxelLandM5FixedTriangleQ18 Down = FVoxelLandM5MeshFeetProposalFixture::Triangle(5.0, 5.0, 5.0);
	Swap(Down.B, Down.C);
	Triangles.Add(Down);
	FVoxelLandM5FixedTriangleQ18 Vertical;
	constexpr int64 Q = int64(1) << 18;
	Vertical.A.Y = -5 * Q;
	Vertical.A.Z = -5 * Q;
	Vertical.B.Y = 5 * Q;
	Vertical.B.Z = -5 * Q;
	Vertical.C.Z = 5 * Q;
	Triangles.Add(Vertical);
	const FVoxelLandM5Diagnostic Slope =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	// Independent barycentric weights (1/4,1/4,1/2) give height 3/8 cm.
	TestTrue(TEXT("highest upward slope rounds up"),
		Slope.Code == EVoxelLandM5ErrorCode::None && Out.X == 0 && Out.Y == 0 && Out.Z == 1);
	Triangles.Add(FVoxelLandM5MeshFeetProposalFixture::Triangle(2.0, 2.0, 2.0));
	const FVoxelLandM5Diagnostic Flat =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	TestTrue(TEXT("higher exact flat surface wins"), Flat.Code == EVoxelLandM5ErrorCode::None && Out.Z == 2);
	const FVoxelLandM5PointCm Before = Out;
	TArray<FVoxelLandM5FixedTriangleQ18> ExactLimit;
	ExactLimit.Add(FVoxelLandM5MeshFeetProposalFixture::Triangle(2.0, 2.0, 2.0));
	FVoxelLandM5PointCm LimitCenter = Center;
	LimitCenter.Z = 2;
	const FVoxelLandM5Diagnostic AtLimit =
		VoxelLand::M5::MeshFeetProposal::Build(LimitCenter, 2, ExactLimit, Budget.ToSharedRef(), Never, Out);
	TestTrue(
		TEXT("exact flat height at both vertical limits"), AtLimit.Code == EVoxelLandM5ErrorCode::None && Out.Z == 2);
	Triangles.Add(FVoxelLandM5MeshFeetProposalFixture::Triangle(2.0, 2.0, 2.0));
	const FVoxelLandM5Diagnostic Overlap =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	TestTrue(TEXT("equal height ambiguity preserves output"),
		Overlap.Code == EVoxelLandM5ErrorCode::Unproven && Out.X == Before.X && Out.Y == Before.Y && Out.Z == Before.Z);
	Triangles.SetNum(1);
	FVoxelLandM5PointCm Edge = Center;
	Edge.X = 0;
	Edge.Y = -10;
	const FVoxelLandM5Diagnostic Boundary =
		VoxelLand::M5::MeshFeetProposal::Build(Edge, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	TestTrue(TEXT("projection boundary fails atomically"), Boundary.Code == EVoxelLandM5ErrorCode::Unproven
															   && Out.X == Before.X && Out.Y == Before.Y
															   && Out.Z == Before.Z);
	Triangles[0].A.X = (int64(1048576) << 18) + 1;
	const FVoxelLandM5Diagnostic Invalid =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	TestTrue(
		TEXT("invalid mesh fails atomically"), Invalid.Code == EVoxelLandM5ErrorCode::Unproven && Out.Z == Before.Z);
	Triangles[0] = FVoxelLandM5MeshFeetProposalFixture::Triangle(-4.0, -4.0, -4.0);
	Triangles[0].RendererErrorQ18.X = 1;
	const FVoxelLandM5Diagnostic Uncertain =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), Never, Out);
	TestTrue(TEXT("renderer uncertainty fails atomically"),
		Uncertain.Code == EVoxelLandM5ErrorCode::Unproven && Out.Z == Before.Z);
	const FVoxelLandM5Diagnostic Cancel =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Budget.ToSharedRef(), [] { return true; }, Out);
	TestTrue(
		TEXT("cancellation preserves output"), Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Out.Z == Before.Z);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
	if (!TestTrue(TEXT("small work budget"), FVoxelLandM5Budget::TryCreate(1000, 1, Tiny)))
	{
		return false;
	}
	const FVoxelLandM5Diagnostic Exhausted =
		VoxelLand::M5::MeshFeetProposal::Build(Center, -10, Triangles, Tiny.ToSharedRef(), Never, Out);
	TestTrue(TEXT("budget failure preserves output"),
		Exhausted.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Out.Z == Before.Z);
	return true;
}
#endif
