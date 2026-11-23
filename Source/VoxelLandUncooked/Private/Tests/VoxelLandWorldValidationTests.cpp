#if WITH_DEV_AUTOMATION_TESTS
#include "World/VoxelLandWorldValidation.h"

#include "Data/Structs/VoxelLandWorldCommonSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldValidationNativeTest, "VoxelLand.WorldModes.Validation.Native",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldValidationNativeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(1024 * 1024, 100000, Budget))
	{
		AddError(TEXT("Could not create validation budget."));
		return false;
	}
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	FVoxelLandWorldCommonSnapshot Source;
	FString Error;
	const auto NeverCancel = [] { return false; };
	TestFalse(TEXT("unknown world kind rejected in data-only validation"),
		VoxelLandWorldValidation::ValidateSnapshot(Source, static_cast<EVoxelLandWorldKind>(255), Providers,
			Budget.ToSharedRef(), NeverCancel, Error));
	TestTrue(TEXT("unknown kind diagnostic"), Error.Contains(TEXT("Unsupported world kind")));
	const auto Cancelled = [] { return true; };
	TestFalse(TEXT("cancelled data-only validation fails"),
		VoxelLandWorldValidation::ValidateSnapshot(
			Source, EVoxelLandWorldKind::Finite, Providers, Budget.ToSharedRef(), Cancelled, Error));
	TestTrue(TEXT("cancellation diagnostic"), Error.Contains(TEXT("cancelled")));
	return true;
}
#endif
