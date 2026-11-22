#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5TriangleSupport.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5TriangleSupportFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm TriangleSupportPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}

TArray<FVoxelLandM5FloorTriangleCm> TriangleSupportFan()
{
	// z = 3x/4; four triangles meet at the origin, with no rectangular pair.
	const auto A = TriangleSupportPoint(-20, -20, -15);
	const auto B = TriangleSupportPoint(20, -20, 15);
	const auto C = TriangleSupportPoint(20, 20, 15);
	const auto D = TriangleSupportPoint(-20, 20, -15);
	const auto O = TriangleSupportPoint(0, 0, 0);
	const FVoxelLandM5PointCm Ring[] = {A, B, C, D};
	TArray<FVoxelLandM5FloorTriangleCm> Out;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FVoxelLandM5FloorTriangleCm T;
		T.A = Ring[Index];
		T.B = Ring[(Index + 1) % 4];
		T.C = O;
		Out.Add(T);
	}
	return Out;
}

TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> TriangleSupportBudget(int64 Work)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5Budget::TryCreate(1024, Work, Budget);
	return Budget.ToSharedRef();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5TriangleSupportAnalyticTest, "VoxelLand.M5.TriangleSupport.Analytic", M5TriangleSupportFlags)
bool FVoxelLandM5TriangleSupportAnalyticTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::TriangleSupport::CertifyLinearContact;
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5SupportPlane Plane;
	Plane.GradientX = 3;
	Plane.Denominator = 4;
	const auto A = TriangleSupportPoint(1, 0, 2);
	const auto B = TriangleSupportPoint(5, 0, 5);
	const auto Budget = TriangleSupportBudget(10000000);
	TArray<FVoxelLandM5FloorTriangleCm> Triangles = TriangleSupportFan();
	const auto Check = [&](const FVoxelLandM5PointCm& Start, int64 Skin, int32 SlopeN, int32 SlopeD) {
		return CertifyLinearContact(Start, B, 5, 185, Skin, Plane, SlopeN, SlopeD, Triangles, Budget, NeverCancel).Code;
	};
	// Normal = (-3,0,4), length 5. Lower sphere centers at (1,0,7)
	// and (5,0,10) both have normal dot product 25, so distance is
	// exactly 5 cm. This is independent of the production predicate.
	TestEqual(
		TEXT("arbitrary fan, exact sloped contact"), int32(Check(A, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("slope limit below 3/4 rejected"), int32(Check(A, 0, 1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	auto Raised = A;
	++Raised.Z;
	TestEqual(
		TEXT("gap exceeds zero contact skin"), int32(Check(Raised, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(
		TEXT("gap within one centimetre skin"), int32(Check(Raised, 1, 3, 4)), int32(EVoxelLandM5ErrorCode::None));
	auto Penetrating = A;
	--Penetrating.Z;
	TestEqual(TEXT("sphere penetrates support plane"), int32(Check(Penetrating, 1, 3, 4)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.RemoveAt(1);
	TestEqual(
		TEXT("missing triangle, possible hole"), int32(Check(A, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles = TriangleSupportFan();
	const FVoxelLandM5FloorTriangleCm DuplicateTriangle = Triangles[0];
	Triangles.Add(DuplicateTriangle);
	TestEqual(TEXT("overlapping duplicate rejected"), int32(Check(A, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles = TriangleSupportFan();
	Swap(Triangles[0].B, Triangles[0].C);
	TestEqual(TEXT("downward face rejected"), int32(Check(A, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles = TriangleSupportFan();
	++Triangles[0].A.Z;
	TestEqual(TEXT("noncoplanar face rejected"), int32(Check(A, 0, 3, 4)), int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5TriangleSupportFailureTest, "VoxelLand.M5.TriangleSupport.Failures", M5TriangleSupportFlags)
bool FVoxelLandM5TriangleSupportFailureTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::TriangleSupport::CertifyLinearContact;
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5SupportPlane Plane;
	Plane.GradientX = 3;
	Plane.Denominator = 4;
	const auto A = TriangleSupportPoint(1, 0, 2);
	const auto B = TriangleSupportPoint(5, 0, 5);
	const auto Triangles = TriangleSupportFan();
	const auto EmptyBudget = TriangleSupportBudget(0);
	TestEqual(TEXT("budget rejected before scan"),
		int32(CertifyLinearContact(A, B, 5, 185, 0, Plane, 3, 4, Triangles, EmptyBudget, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	int32 Calls = 0;
	const auto CancelDuringScan = [&Calls] {
		return ++Calls == 4;
	};
	const auto Budget = TriangleSupportBudget(1000000);
	TestEqual(TEXT("cancellation during scan"),
		int32(CertifyLinearContact(A, B, 5, 185, 0, Plane, 3, 4, Triangles, Budget, CancelDuringScan).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("height shorter than diameter"),
		int32(CertifyLinearContact(A, B, 5, 9, 0, Plane, 3, 4, Triangles, Budget, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	const auto Far = TriangleSupportPoint(1048576, 0, 0);
	TestEqual(TEXT("capsule footprint outside envelope"),
		int32(
			CertifyLinearContact(Far, Far, 5, 185, 0, FVoxelLandM5SupportPlane{}, 0, 1, Triangles, Budget, NeverCancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::OutOfDomain));
	// The footprint center lies on the shared diagonal. An alternate strict
	// witness proves coverage without treating the diagonal as a boundary.
	TArray<FVoxelLandM5FloorTriangleCm> CenteredQuad;
	FVoxelLandM5FloorTriangleCm Lower;
	Lower.A = TriangleSupportPoint(-20, -20, 0);
	Lower.B = TriangleSupportPoint(20, -20, 0);
	Lower.C = TriangleSupportPoint(20, 20, 0);
	FVoxelLandM5FloorTriangleCm Upper;
	Upper.A = Lower.A;
	Upper.B = Lower.C;
	Upper.C = TriangleSupportPoint(-20, 20, 0);
	CenteredQuad.Add(Lower);
	CenteredQuad.Add(Upper);
	TestEqual(TEXT("shared diagonal through footprint center is supported"),
		int32(CertifyLinearContact(TriangleSupportPoint(-2, 0, 0), TriangleSupportPoint(2, 0, 0), 2, 10, 0,
			FVoxelLandM5SupportPlane{}, 0, 1, CenteredQuad, Budget, NeverCancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::None));
	CenteredQuad.RemoveAt(1);
	TestEqual(TEXT("missing adjacent triangle is not covered"),
		int32(CertifyLinearContact(TriangleSupportPoint(-2, 0, 0), TriangleSupportPoint(2, 0, 0), 2, 10, 0,
			FVoxelLandM5SupportPlane{}, 0, 1, CenteredQuad, Budget, NeverCancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	// Four disjoint outside triangles can each touch a footprint corner without
	// covering any interior. Corner tests alone must not certify this case.
	TArray<FVoxelLandM5FloorTriangleCm> CornerOnly;
	for (int64 X : {-1ll, 11ll})
	{
		for (int64 Y : {-1ll, 1ll})
		{
			FVoxelLandM5FloorTriangleCm T;
			T.A = TriangleSupportPoint(X, Y, 0);
			const int64 Direction = X < 0 ? -1 : 1;
			T.B = TriangleSupportPoint(X + 2 * Direction, Y - Direction, 0);
			T.C = TriangleSupportPoint(X + 2 * Direction, Y + Direction, 0);
			CornerOnly.Add(T);
		}
	}
	TestEqual(TEXT("corner-only disconnected patches do not cover interior"),
		int32(CertifyLinearContact(TriangleSupportPoint(0, 0, 0), TriangleSupportPoint(10, 0, 0), 1, 4, 0,
			FVoxelLandM5SupportPlane{}, 0, 1, CornerOnly, Budget, NeverCancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}
#endif
