#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5PiecewiseSupport.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5PiecewiseFixture final
{
public:
	static bool NeverCancel() { return false; }
	static bool AlwaysCancel() { return true; }
	static FVoxelLandM5FixedPointQ18 Point(int64 X, int64 Y, int64 Z)
	{
		FVoxelLandM5FixedPointQ18 P;
		P.X = X * (int64(1) << 18);
		P.Y = Y * (int64(1) << 18);
		P.Z = Z * (int64(1) << 18);
		return P;
	}
	static FVoxelLandM5FixedTriangleQ18 Triangle(
		const FVoxelLandM5FixedPointQ18& A, const FVoxelLandM5FixedPointQ18& B, const FVoxelLandM5FixedPointQ18& C)
	{
		FVoxelLandM5FixedTriangleQ18 T;
		T.A = A;
		T.B = B;
		T.C = C;
		return T;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PiecewiseSupportTest, "VoxelLand.M5.PiecewiseSupport.Kink",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5PiecewiseSupportTest::RunTest(const FString& Parameters)
{
	using F = FVoxelLandM5PiecewiseFixture;
	using VoxelLand::M5::PiecewiseSupport::CertifyLinearContact;
	using VoxelLand::M5::PiecewiseSupport::CertifyLinearContactWithMargin;
	const FVoxelLandM5FixedPointQ18 A = F::Point(-40, -40, 20), B = F::Point(0, -40, 0), C = F::Point(40, -40, 20);
	const FVoxelLandM5FixedPointQ18 D = F::Point(-40, 40, 20), E = F::Point(0, 40, 0), G = F::Point(40, 40, 20);
	TArray<FVoxelLandM5FixedTriangleQ18> Faces = {
		F::Triangle(A, B, E), F::Triangle(A, E, D), F::Triangle(B, C, G), F::Triangle(B, G, E)};
	const FVoxelLandM5FixedPointQ18 Start = F::Point(-10, 0, 8), End = F::Point(10, 0, 8);
	const int64 Q = int64(1) << 18;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(1000000, 10000000, Budget))
	{
		return false;
	}
	// Normals (1,0,2), (-1,0,2). Center z=18; for each x in
	// [-10,10], the nearest plane distance ranges 26/sqrt(5) to
	// 36/sqrt(5), inside [10,17]. Projections lie in their triangles.
	TestEqual(TEXT("continuous contact across kink"),
		CertifyLinearContact(Start, End, 10 * Q, 20 * Q, 7 * Q, 1, 2, Faces, Budget.ToSharedRef(), F::NeverCancel).Code,
		EVoxelLandM5ErrorCode::None);
	// At the limiting endpoint, gap=26, gradient=1 and denominator=2.
	// Margin 1 gives (26-11)^2 + 20^2 = 625 > 22^2.
	// Margin 3 gives (26-13)^2 + 20^2 = 569 < 26^2.
	TestEqual(TEXT("one-unit margin safe on every face"),
		CertifyLinearContactWithMargin(
			Start, End, 10 * Q, 20 * Q, 7 * Q, Q, 1, 2, Faces, Budget.ToSharedRef(), F::NeverCancel)
			.Code,
		EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("excess margin rejected"),
		CertifyLinearContactWithMargin(
			Start, End, 10 * Q, 20 * Q, 7 * Q, 3 * Q, 1, 2, Faces, Budget.ToSharedRef(), F::NeverCancel)
			.Code,
		EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("zero margin preserves nominal proof"),
		CertifyLinearContactWithMargin(
			Start, End, 10 * Q, 20 * Q, 7 * Q, 0, 1, 2, Faces, Budget.ToSharedRef(), F::NeverCancel)
			.Code,
		EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("slope bound enforced"),
		CertifyLinearContact(Start, End, 10 * Q, 20 * Q, 7 * Q, 1, 3, Faces, Budget.ToSharedRef(), F::NeverCancel).Code,
		EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("penetration rejected"),
		CertifyLinearContact(F::Point(-10, 0, 0), F::Point(10, 0, 0), 10 * Q, 20 * Q, 7 * Q, 1, 2, Faces,
			Budget.ToSharedRef(), F::NeverCancel)
			.Code,
		EVoxelLandM5ErrorCode::Unproven);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
	if (!FVoxelLandM5Budget::TryCreate(1000000, 0, Tiny))
	{
		return false;
	}
	TestEqual(TEXT("work budget enforced"),
		CertifyLinearContact(Start, End, 10 * Q, 20 * Q, 7 * Q, 1, 2, Faces, Tiny.ToSharedRef(), F::NeverCancel).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("cancellation propagated"),
		CertifyLinearContact(Start, End, 10 * Q, 20 * Q, 7 * Q, 1, 2, Faces, Budget.ToSharedRef(), F::AlwaysCancel)
			.Code,
		EVoxelLandM5ErrorCode::Cancelled);
	Faces[2].A.Z += Q;
	TestEqual(TEXT("discontinuous shared vertex rejected"),
		CertifyLinearContact(Start, End, 10 * Q, 20 * Q, 7 * Q, 1, 2, Faces, Budget.ToSharedRef(), F::NeverCancel).Code,
		EVoxelLandM5ErrorCode::Unproven);
	return true;
}
#endif
