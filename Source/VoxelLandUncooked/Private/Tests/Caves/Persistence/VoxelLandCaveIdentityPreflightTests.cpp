#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Misc/AutomationTest.h"
#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflight.h"
#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflightLimits.h"
#include "World/Caves/Persistence/VoxelLandCaveIdentityPreflightStats.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5IdentityPreflightTest, "VoxelLand.M5.Identity.Preflight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5IdentityPreflightTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5WorldSnapshot Snapshot;
	auto& Channel = Snapshot.Base.Continental;
	Channel.Source.Parameters.AddDefaulted_GetRef().Name = TEXT("abc");
	auto& Node = Channel.Source.Nodes.AddDefaulted_GetRef();
	Node.Configuration.SetNumZeroed(3);
	Node.Pins.AddDefaulted_GetRef().Name = TEXT("xy");
	Channel.Source.Links.AddDefaulted(2);
	auto& Function = Channel.Source.Functions.AddDefaulted_GetRef();
	Function.AssetPath = TEXT("f");
	Function.Inputs.AddDefaulted_GetRef().Name = TEXT("a");
	auto& Nested = Function.Nodes.AddDefaulted_GetRef();
	Nested.Configuration.SetNumZeroed(4);
	Nested.Pins.AddDefaulted_GetRef().Name = TEXT("b");
	Function.Links.AddDefaulted();
	Channel.Overrides.AddDefaulted();
	Channel.CurveKeys.AddDefaulted();
	Channel.CookedCurve.SourceKeys.AddDefaulted();
	Channel.CookedCurve.SamplesQ8.AddDefaulted(2);
	Channel.Payload.Instructions.AddDefaulted(2);
	Channel.Payload.Constants.AddDefaulted(3);
	Channel.Payload.Parameters.AddDefaulted_GetRef().Name = TEXT("zz");
	Channel.Payload.Seeds.AddDefaulted();
	Channel.Payload.Noise.AddDefaulted();
	auto& External = Channel.Payload.ExternalOperators.AddDefaulted_GetRef();
	External.Inputs.AddDefaulted(2);
	External.Configuration.SetNumZeroed(5);

	FVoxelLandM5IdentityPreflightLimits Limits;
	Limits.MaxElements = 26;
	Limits.MaxStringCodeUnits = 10;
	Limits.MaxConfigurationBytes = 12;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(0, 12, Budget)))
	{
		return false;
	}
	FVoxelLandM5IdentityPreflightStats Stats;
	auto NeverCancel = [] {
		return false;
	};
	auto Result = VoxelLand::M5::IdentityPreflight::Inspect(Snapshot, Limits, Budget.ToSharedRef(), NeverCancel, Stats);
	TestEqual(TEXT("exact limits succeed"), Result.Code, EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("independent element count"), Stats.Elements, int64(26));
	TestEqual(TEXT("independent character count"), Stats.StringCodeUnits, int64(10));
	TestEqual(TEXT("independent configuration count"), Stats.ConfigurationBytes, int64(12));
	int64 Live = -1, Peak = -1, Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("twelve inspected records"), Work, int64(12));
	TestEqual(TEXT("no scratch reservation"), Live, int64(0));

	for (int32 Case = 0; Case < 6; ++Case)
	{
		FVoxelLandM5IdentityPreflightLimits Bad = Limits;
		if (Case == 0)
		{
			--Bad.MaxElements;
		}
		if (Case == 1)
		{
			--Bad.MaxStringCodeUnits;
		}
		if (Case == 2)
		{
			--Bad.MaxConfigurationBytes;
		}
		if (Case == 3)
		{
			Bad.MaxElements = -1;
		}
		if (Case == 4)
		{
			Bad.MaxStringCodeUnits = -1;
		}
		if (Case == 5)
		{
			Bad.MaxConfigurationBytes = -1;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!TestTrue(TEXT("failure budget created"), FVoxelLandM5Budget::TryCreate(0, 100, Other)))
		{
			return false;
		}
		FVoxelLandM5IdentityPreflightStats Retained;
		Retained.Elements = 777;
		Result = VoxelLand::M5::IdentityPreflight::Inspect(Snapshot, Bad, Other.ToSharedRef(), NeverCancel, Retained);
		TestEqual(TEXT("limit fails"), Result.Code,
			Case >= 3 ? EVoxelLandM5ErrorCode::InvalidInput : EVoxelLandM5ErrorCode::BudgetExceeded);
		TestEqual(TEXT("failure preserves output"), Retained.Elements, int64(777));
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Short;
	if (!TestTrue(TEXT("short budget created"), FVoxelLandM5Budget::TryCreate(0, 11, Short)))
	{
		return false;
	}
	Stats.Elements = 777;
	Result = VoxelLand::M5::IdentityPreflight::Inspect(Snapshot, Limits, Short.ToSharedRef(), NeverCancel, Stats);
	TestEqual(TEXT("one less work fails"), Result.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("work failure preserves output"), Stats.Elements, int64(777));
	int32 Calls = 0;
	auto Cancel = [&Calls] {
		return ++Calls == 5;
	};
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> CancelBudget;
	if (!TestTrue(TEXT("cancel budget created"), FVoxelLandM5Budget::TryCreate(0, 100, CancelBudget)))
	{
		return false;
	}
	Result = VoxelLand::M5::IdentityPreflight::Inspect(Snapshot, Limits, CancelBudget.ToSharedRef(), Cancel, Stats);
	TestEqual(TEXT("mid traversal cancellation"), Result.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancel preserves output"), Stats.Elements, int64(777));

	FVoxelLandM5WorldSnapshot EmptySnapshot;
	FVoxelLandM5IdentityPreflightLimits EmptyLimits;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (!TestTrue(TEXT("empty budget created"), FVoxelLandM5Budget::TryCreate(0, 3, EmptyBudget)))
	{
		return false;
	}
	FVoxelLandM5IdentityPreflightStats EmptyStats;
	EmptyStats.Elements = 777;
	Result = VoxelLand::M5::IdentityPreflight::Inspect(
		EmptySnapshot, EmptyLimits, EmptyBudget.ToSharedRef(), NeverCancel, EmptyStats);
	TestEqual(TEXT("empty inventory succeeds"), Result.Code, EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("empty inventory counts zero"), EmptyStats.Elements, int64(0));
	TestFalse(TEXT("empty inventory has no opaque soft paths"), EmptyStats.bHasOpaqueSoftPaths);

	FVoxelLandM5WorldSnapshot OpaqueSnapshot;
	OpaqueSnapshot.Base.Continental.Source.Nodes.AddDefaulted_GetRef().FunctionPath =
		FSoftObjectPath(TEXT("/Game/VoxelLandTests/Function.Function"));
	FVoxelLandM5IdentityPreflightLimits OpaqueLimits;
	OpaqueLimits.MaxElements = 1;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OpaqueBudget;
	if (!TestTrue(TEXT("opaque budget created"), FVoxelLandM5Budget::TryCreate(0, 4, OpaqueBudget)))
	{
		return false;
	}
	Result = VoxelLand::M5::IdentityPreflight::Inspect(
		OpaqueSnapshot, OpaqueLimits, OpaqueBudget.ToSharedRef(), NeverCancel, EmptyStats);
	TestEqual(TEXT("opaque path inventory succeeds"), Result.Code, EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("opaque path is flagged"), EmptyStats.bHasOpaqueSoftPaths);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OpaqueShort;
	if (!TestTrue(TEXT("short opaque budget created"), FVoxelLandM5Budget::TryCreate(0, 2, OpaqueShort)))
	{
		return false;
	}
	EmptyStats.bHasOpaqueSoftPaths = false;
	EmptyStats.Elements = 777;
	Result = VoxelLand::M5::IdentityPreflight::Inspect(
		OpaqueSnapshot, OpaqueLimits, OpaqueShort.ToSharedRef(), NeverCancel, EmptyStats);
	TestEqual(TEXT("failure after opaque path"), Result.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestFalse(TEXT("failed opaque inventory preserves flag"), EmptyStats.bHasOpaqueSoftPaths);
	TestEqual(TEXT("failed opaque inventory preserves count"), EmptyStats.Elements, int64(777));

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> FinalCancelBudget;
	if (!TestTrue(TEXT("final cancellation budget created"), FVoxelLandM5Budget::TryCreate(0, 3, FinalCancelBudget)))
	{
		return false;
	}
	int32 FinalCalls = 0;
	auto FinalCancel = [&FinalCalls] {
		return ++FinalCalls == 5;
	};
	EmptyStats.Elements = 777;
	Result = VoxelLand::M5::IdentityPreflight::Inspect(
		EmptySnapshot, EmptyLimits, FinalCancelBudget.ToSharedRef(), FinalCancel, EmptyStats);
	TestEqual(TEXT("cancellation before publication"), Result.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("final cancellation preserves output"), EmptyStats.Elements, int64(777));
	return true;
}
#endif
