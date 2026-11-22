#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5PlanPreparation.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PreparationAdmissionTest,
	"VoxelLand.M5.PlanPreparation.SharedBudgetPending",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5PreparationAdmissionTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -100;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 100;
	S.Topology.MinChambers = S.Topology.MaxChambers = 3;
	S.Topology.MinLoops = S.Topology.MaxLoops = S.Topology.EntranceCount = 0;
	S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 1;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
	S.Capsule.StandingHeightCm = 2;
	S.Capsule.RadiusCm = 1;
	S.Capsule.ClearanceMarginCm = 0;
	S.Limits.MaxCandidates = 3;
	S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
	S.Limits.MaxBytes = 100000000;
	S.Limits.MaxValidationWork = 1000000000;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget))
		|| !TestTrue(TEXT("budget valid"), Budget.IsValid()))
	{
		return false;
	}
	const TArray<uint8> Bytes = {1, 2, 3};
	const FVoxelLandM5RegionCoord Region;
	int32 VerifyCalls = 0, SurfaceCalls = 0, CertifyCalls = 0;
	const auto Verify = [&VerifyCalls](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
							const FVoxelLandM5CaveSettings&, int64, const FVoxelLandM5RegionCoord&,
							TConstArrayView<uint8> Identity, TFunctionRef<bool()>) {
		++VerifyCalls;
		FVoxelLandM5Diagnostic Error;
		if (Identity.Num() != 3 || Identity[0] != 1 || !InBudget->TryChargeWork(1))
		{
			Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		return Error;
	};
	const auto Surface = [&SurfaceCalls](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32,
							 int64&, int64&, int64&, int32&, bool&, bool&) {
		++SurfaceCalls;
		return FVoxelLandM5Diagnostic();
	};
	const auto Certify = [&CertifyCalls](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
							 const FVoxelLandM5CaveSettings&, const FVoxelLandM5CavePlan&, TConstArrayView<uint8>,
							 TFunctionRef<bool()>) {
		++CertifyCalls;
		return FVoxelLandM5Diagnostic();
	};
	TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe> Out =
		MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	const auto Prior = Out;
	FVoxelLandM5Diagnostic D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Bytes, Budget.ToSharedRef(), Verify, Surface, Certify, [] { return false; }, Out);
	TestTrue(TEXT("uncertified final field cannot publish"), D.Code == EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("descriptor verifier was called"), VerifyCalls, 1);
	TestEqual(TEXT("no entrances means no surface query"), SurfaceCalls, 0);
	TestEqual(TEXT("untrusted final certifier is not called"), CertifyCalls, 0);
	TestTrue(TEXT("prior plan remains"), Out == Prior);
	int64 Live = -1, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("all temporary leases released"), Live, int64(0));
	TestTrue(TEXT("scratch was charged"), Peak > 0 && Work > 0);
	D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Bytes, Budget.ToSharedRef(), Verify, Surface, Certify, [] { return true; }, Out);
	TestTrue(TEXT("pre-cancelled request reports cancellation"), D.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancelled request preserves prior plan"), Out == Prior);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tiny;
	if (TestTrue(TEXT("tiny budget created"), FVoxelLandM5Budget::TryCreate(1, S.Limits.MaxValidationWork, Tiny))
		&& Tiny.IsValid())
	{
		D = VoxelLand::M5::PlanPreparation::Build(
			S, 42, Region, Bytes, Tiny.ToSharedRef(), Verify, Surface, Certify, [] { return false; }, Out);
		TestTrue(TEXT("identity admission fails"), D.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
		TestTrue(TEXT("admission failure preserves prior plan"), Out == Prior);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PreparationProviderTest,
	"VoxelLand.M5.PlanPreparation.ProviderAndCumulativeAdmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5PreparationProviderTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -1000;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 1000;
	S.Topology.MinChambers = S.Topology.MaxChambers = 3;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	S.Topology.EntranceCount = 1;
	S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 1;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
	S.Capsule.StandingHeightCm = 2;
	S.Capsule.RadiusCm = 1;
	S.Capsule.ClearanceMarginCm = 0;
	S.Limits.MaxCandidates = 3;
	S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
	S.Limits.MaxBytes = 100000000;
	S.Limits.MaxValidationWork = 1000000000;
	const FVoxelLandM5RegionCoord Region;
	const TArray<uint8> Identity = {1, 2, 3};
	TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe> Out =
		MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	const auto Prior = Out;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	int32 SurfaceCalls = 0, CertifyCalls = 0;
	bool bWrongBudget = false, bCancel = false;
	TWeakPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SurfaceLease;
	bool bSurfaceReserved = false;
	const auto Verify = [](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, const FVoxelLandM5CaveSettings&,
							int64, const FVoxelLandM5RegionCoord&, TConstArrayView<uint8>, TFunctionRef<bool()>) {
		FVoxelLandM5Diagnostic D;
		if (!Shared->TryChargeWork(1))
		{
			D.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		return D;
	};
	const auto Certify = [&CertifyCalls](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
							 const FVoxelLandM5CaveSettings&, const FVoxelLandM5CavePlan&, TConstArrayView<uint8>,
							 TFunctionRef<bool()>) {
		++CertifyCalls;
		return FVoxelLandM5Diagnostic();
	};
	const auto Cancel = [&bCancel] {
		return bCancel;
	};
	const auto ResetBudget = [&]() {
		Budget.Reset();
		return FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget);
	};
	if (!TestTrue(TEXT("provider budget created"), ResetBudget()))
	{
		return false;
	}
	const auto FailingSurface = [&](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, int64, int64, int32,
									int64&, int64&, int64&, int32&, bool&, bool&) {
		++SurfaceCalls;
		bWrongBudget |= &Shared.Get() != Budget.Get();
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> LocalLease;
		bSurfaceReserved = Shared->TryReserveBytes(256, LocalLease);
		SurfaceLease = LocalLease;
		FVoxelLandM5Diagnostic D;
		D.Code = bSurfaceReserved && Shared->TryChargeWork(1) ? EVoxelLandM5ErrorCode::ProviderUnavailable
															  : EVoxelLandM5ErrorCode::BudgetExceeded;
		return D;
	};
	FVoxelLandM5Diagnostic D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Identity, Budget.ToSharedRef(), Verify, FailingSurface, Certify, Cancel, Out);
	TestTrue(TEXT("provider failure propagates"), D.Code == EVoxelLandM5ErrorCode::ProviderUnavailable);
	TestEqual(TEXT("surface was queried"), SurfaceCalls, 1);
	TestFalse(TEXT("provider received same budget"), bWrongBudget);
	TestTrue(TEXT("failing provider reserved scratch"), bSurfaceReserved);
	TestFalse(TEXT("failing provider released its scratch lease"), SurfaceLease.IsValid());
	TestTrue(TEXT("provider failure leaves output untouched"), Out == Prior);
	int64 Live = -1, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("provider failure releases all scratch"), Live, int64(0));
	TestTrue(TEXT("earlier stages charged work"), Work > 1);
	if (!TestTrue(TEXT("cancel budget created"), ResetBudget()))
	{
		return false;
	}
	SurfaceCalls = 0;
	bSurfaceReserved = false;
	const auto CancelAfterSurface = [&](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, int64, int64, int32,
										int64& Z, int64& MinZ, int64& MaxZ, int32& Slope, bool& Protected,
										bool& Flooded) {
		++SurfaceCalls;
		bWrongBudget |= &Shared.Get() != Budget.Get();
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> LocalLease;
		bSurfaceReserved = Shared->TryReserveBytes(256, LocalLease);
		SurfaceLease = LocalLease;
		Z = MinZ = MaxZ = 0;
		Slope = 0;
		Protected = Flooded = false;
		FVoxelLandM5Diagnostic Result;
		if (!bSurfaceReserved || !Shared->TryChargeWork(1))
		{
			Result.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		bCancel = true;
		return Result;
	};
	D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Identity, Budget.ToSharedRef(), Verify, CancelAfterSurface, Certify, Cancel, Out);
	TestTrue(TEXT("cancellation after query propagates"), D.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancellation happened after one surface query"), SurfaceCalls, 1);
	TestFalse(TEXT("same budget passed to query"), bWrongBudget);
	TestTrue(TEXT("successful provider reserved scratch"), bSurfaceReserved);
	TestFalse(TEXT("successful provider released its scratch lease"), SurfaceLease.IsValid());
	TestTrue(TEXT("cancel leaves output untouched"), Out == Prior);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("cancel releases all scratch"), Live, int64(0));
	bCancel = false;
	if (!TestTrue(TEXT("work-exhaustion budget created"), ResetBudget()))
	{
		return false;
	}
	SurfaceCalls = 0;
	const auto ExhaustWork = [&](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, int64, int64, int32,
								 int64&, int64&, int64&, int32&, bool&, bool&) {
		++SurfaceCalls;
		int64 LimitBytes = 0, LimitWork = 0, LocalLive = 0, LocalPeak = 0, Used = 0;
		Shared->GetLimits(LimitBytes, LimitWork);
		Shared->GetUsage(LocalLive, LocalPeak, Used);
		FVoxelLandM5Diagnostic Result;
		if (!Shared->TryChargeWork(LimitWork - Used) || !Shared->TryChargeWork(1))
		{
			Result.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		return Result;
	};
	D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Identity, Budget.ToSharedRef(), Verify, ExhaustWork, Certify, Cancel, Out);
	TestTrue(TEXT("cumulative work exhaustion propagates"), D.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("work exhaustion after candidate and entrance"), SurfaceCalls, 1);
	TestTrue(TEXT("work exhaustion leaves output untouched"), Out == Prior);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("work exhaustion releases all scratch"), Live, int64(0));
	TestEqual(TEXT("work remains charged"), Work, S.Limits.MaxValidationWork);
	TestEqual(TEXT("final validator never called"), CertifyCalls, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PreparationIndexBudgetTest, "VoxelLand.M5.PlanPreparation.IndexAdmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5PreparationIndexBudgetTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -100;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 100;
	S.Topology.MinChambers = S.Topology.MaxChambers = 3;
	S.Topology.MinLoops = S.Topology.MaxLoops = S.Topology.EntranceCount = 0;
	S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 1;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
	S.Capsule.StandingHeightCm = 2;
	S.Capsule.RadiusCm = 1;
	S.Capsule.ClearanceMarginCm = 0;
	S.Limits.MaxCandidates = 3;
	S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
	S.Limits.MaxBytes = 100000000;
	S.Limits.MaxValidationWork = 1000000000;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("index budget created"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget))
		|| !Budget.IsValid())
	{
		return false;
	}
	const auto Verify = [](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, const FVoxelLandM5CaveSettings&,
							int64, const FVoxelLandM5RegionCoord&, TConstArrayView<uint8>, TFunctionRef<bool()>) {
		FVoxelLandM5Diagnostic D;
		if (!Shared->TryChargeWork(1))
		{
			D.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		return D;
	};
	const auto Surface = [](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, int64, int64, int32, int64&, int64&,
							 int64&, int32&, bool&, bool&) {
		return FVoxelLandM5Diagnostic();
	};
	const auto Certify = [](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, const FVoxelLandM5CaveSettings&,
							 const FVoxelLandM5CavePlan&, TConstArrayView<uint8>, TFunctionRef<bool()>) {
		return FVoxelLandM5Diagnostic();
	};
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Occupy;
	int64 PreviousWork = 0;
	bool bOccupiedAtIndex = false;
	// With three chambers and two segments, the index input stage charges five
	// work units before its first per-entry cancellation poll. Capture this
	// deterministic boundary and exhaust memory while the candidate and index
	// entry arrays are live. Production cancellation predicates must not allocate.
	const auto OccupyAtIndex = [&]() {
		int64 Live = 0, Peak = 0, Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		if (!bOccupiedAtIndex && Work - PreviousWork == 5 && Live < S.Limits.MaxBytes)
		{
			bOccupiedAtIndex = Budget->TryReserveBytes(S.Limits.MaxBytes - Live, Occupy);
		}
		PreviousWork = Work;
		return false;
	};
	const TArray<uint8> Identity = {1, 2, 3};
	const FVoxelLandM5RegionCoord Region;
	TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe> Out =
		MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	const auto Prior = Out;
	const FVoxelLandM5Diagnostic D = VoxelLand::M5::PlanPreparation::Build(
		S, 42, Region, Identity, Budget.ToSharedRef(), Verify, Surface, Certify, OccupyAtIndex, Out);
	TestTrue(TEXT("index boundary was reached"), bOccupiedAtIndex);
	TestTrue(TEXT("index memory admission fails"), D.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("index failure preserves prior output"), Out == Prior);
	Occupy.Reset();
	int64 Live = -1, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("all reservations released after held lease"), Live, int64(0));
	return true;
}
#endif
