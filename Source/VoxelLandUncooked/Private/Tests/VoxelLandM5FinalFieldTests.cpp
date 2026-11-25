#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5FinalFieldInputs.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5FinalField.h"
#include "Generation/VoxelLandM5PocketMapping.h"
#include "Generation/VoxelLandM5PocketNoise.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FinalFieldTest, "VoxelLand.M5.FinalField.SamplesAndAdmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FinalFieldTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget)))
	{
		return false;
	}
	const TArray<int16> Baseline = {-3000, -3000, -3000};
	const TArray<uint8> Allowed = {1, 1, 1};
	const TArray<uint8> Protection = {0, 1, 0};
	FVoxelLandM5FinalFieldInputs Inputs;
	Inputs.Size = FIntVector(3, 1, 1);
	Inputs.OriginCm.X = -100;
	Inputs.SpacingCm = 100;
	Inputs.VoxelSizeCm = 100;
	Inputs.Baseline = Baseline;
	Inputs.PocketAllowed = Allowed;
	Inputs.Protected = Protection;
	Inputs.Pockets.bEnabled = true;
	Inputs.Pockets.LatticeSpacingCm = 100;
	Inputs.Pockets.ThresholdQ16 = 0;
	Inputs.Pockets.AmplitudeCm = 400;
	Inputs.WorldSeed = 42;
	Inputs.StageId = FGuid(1, 2, 3, 4);
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> First;
	FVoxelLandM5Diagnostic Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, First);
	if (!TestEqual(TEXT("field built"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("field exists"), First.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("protected sample unchanged"), First->GetDensity()[1], int16(-3000));
	int32 Noise = 0;
	bool bHas = false;
	int64 Distance = 0;
	if (!TestTrue(TEXT("independent noise sample"),
			VoxelLand::M5::PocketNoise::TrySample(-100, 0, 0, 100, 42, Inputs.StageId, 0, Never, Noise))
		|| !TestTrue(TEXT("independent pocket mapping"),
			VoxelLand::M5::PocketMapping::TryMap(true, Noise, 0, 400, bHas, Distance)))
	{
		return false;
	}
	const int64 Numerator = -Distance * 4;
	const int64 Rounded = (Numerator + 50) / 100;
	const int16 Expected = bHas ? static_cast<int16>(FMath::Clamp<int64>(Rounded, -3000, MAX_int16)) : int16(-3000);
	TestEqual(TEXT("mapped pocket uses reference conversion"), First->GetDensity()[0], Expected);
	// Shift the grid by one sample: shared global positions must retain values.
	const TArray<uint8> NoProtection = {0, 0, 0};
	Inputs.OriginCm.X = 100;
	Inputs.Protected = NoProtection;
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Shifted;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Shifted);
	if (!TestEqual(TEXT("shifted field built"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("shifted field exists"), Shifted.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("shared global X sample agrees"), First->GetDensity()[2], Shifted->GetDensity()[0]);
	for (int32 Axis = 1; Axis <= 2; ++Axis)
	{
		Inputs.Size = Axis == 1 ? FIntVector(1, 3, 1) : FIntVector(1, 1, 3);
		Inputs.OriginCm = {};
		if (Axis == 1)
		{
			Inputs.OriginCm.Y = -100;
		}
		else
		{
			Inputs.OriginCm.Z = -100;
		}
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Left;
		TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Right;
		Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Left);
		if (!TestEqual(TEXT("axis field built"), Error.Code, EVoxelLandM5ErrorCode::None) || !Left.IsValid())
		{
			return false;
		}
		if (Axis == 1)
		{
			Inputs.OriginCm.Y = 100;
		}
		else
		{
			Inputs.OriginCm.Z = 100;
		}
		Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Right);
		if (!TestEqual(TEXT("shifted axis field built"), Error.Code, EVoxelLandM5ErrorCode::None) || !Right.IsValid())
		{
			return false;
		}
		TestEqual(Axis == 1 ? TEXT("shared global Y sample agrees") : TEXT("shared global Z sample agrees"),
			Left->GetDensity()[2], Right->GetDensity()[0]);
	}
	Inputs.Size = FIntVector(3, 1, 1);
	Inputs.OriginCm = {};
	Inputs.OriginCm.X = 100;
	Inputs.Pockets.AmplitudeCm = 0;
	Inputs.Protected = {};
	Inputs.PocketAllowed = {};
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Disabled;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	if (!TestEqual(TEXT("zero amplitude builds"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("disabled field exists"), Disabled.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("zero amplitude preserves baseline"),
		Disabled->GetDensity().Num() == 3 && Disabled->GetDensity()[0] == -3000 && Disabled->GetDensity()[1] == -3000
			&& Disabled->GetDensity()[2] == -3000);
	TSharedPtr<FVoxelLandM5CavePlan, ESPMode::ThreadSafe> Plan =
		MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.CenterCm.X = 100;
	Chamber.RadiusCm = 100;
	Plan->Chambers.Add(Chamber);
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	Plans.Add(Plan);
	Inputs.bNetworkEnabled = true;
	Inputs.Plans = Plans;
	Inputs.NetworkAllowed = Allowed;
	Inputs.Protected = Protection;
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Network;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Network);
	if (!TestEqual(TEXT("network field built"), Error.Code, EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("network field exists"), Network.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("network chamber center"), Network->GetDensity()[0], int16(1024));
	TestEqual(TEXT("network respects protection"), Network->GetDensity()[1], int16(-3000));
	Inputs.Pockets.AmplitudeCm = 400;
	Inputs.PocketAllowed = Allowed;
	Inputs.Protected = NoProtection;
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Combined;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Combined);
	if (!TestEqual(TEXT("combined field built"), Error.Code, EVoxelLandM5ErrorCode::None) || !Combined.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("combined field takes union"), Combined->GetDensity()[0],
		FMath::Max(Network->GetDensity()[0], Shifted->GetDensity()[0]));
	Inputs.bNetworkEnabled = false;
	Inputs.OriginCm.X = 1048577;
	Inputs.Pockets.AmplitudeCm = 0;
	Inputs.Protected = {};
	Inputs.PocketAllowed = {};
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Outside;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Outside);
	TestEqual(TEXT("zero amplitude outside cave domain"), Error.Code, EVoxelLandM5ErrorCode::None);
	Inputs.Pockets.bEnabled = false;
	Inputs.StageId = FGuid();
	Inputs.Pockets.AmplitudeCm = -1;
	Inputs.PocketNoiseVersion = 2;
	Inputs.CompositionVersion = 1;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Outside);
	TestEqual(TEXT("disabled outside cave domain ignores unused settings"), Error.Code, EVoxelLandM5ErrorCode::None);
	const TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Before = Disabled;
	Inputs.Pockets.bEnabled = true;
	Inputs.Pockets.AmplitudeCm = 0;
	Inputs.CompositionVersion = 2;
	Inputs.PocketNoiseVersion = 1;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("enabled zero amplitude rejects invalid stage ID"), Error.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid stage ID retains result"), Disabled == Before);
	Inputs.StageId = FGuid(1, 2, 3, 4);
	Inputs.PocketNoiseVersion = 2;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("zero amplitude rejects unsupported pocket version"), Error.Code,
		EVoxelLandM5ErrorCode::UnsupportedVersion);
	TestTrue(TEXT("version failure retains result"), Disabled == Before);
	Inputs.PocketNoiseVersion = 1;
	Inputs.CompositionVersion = 1;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("zero amplitude rejects unsupported composition version"), Error.Code,
		EVoxelLandM5ErrorCode::UnsupportedVersion);
	TestTrue(TEXT("composition version failure retains result"), Disabled == Before);
	Inputs.CompositionVersion = 2;
	Inputs.OriginCm.X = 100;
	Inputs.Pockets.AmplitudeCm = 400;
	Inputs.PocketAllowed = Allowed;
	Inputs.Protected = Protection;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (!TestTrue(TEXT("empty budget created"), FVoxelLandM5Budget::TryCreate(0, 0, EmptyBudget)))
	{
		return false;
	}
	Error = FVoxelLandM5FinalField::Build(Inputs, EmptyBudget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("work budget rejected"), Error.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("budget failure retains result"), Disabled == Before);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoBytes;
	if (!TestTrue(TEXT("zero memory budget created"), FVoxelLandM5Budget::TryCreate(0, 100000, NoBytes)))
	{
		return false;
	}
	Error = FVoxelLandM5FinalField::Build(Inputs, NoBytes.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("memory budget rejected"), Error.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("memory failure retains result"), Disabled == Before);
	TArray<uint8> InvalidProtection = {0, 2, 0};
	Inputs.Protected = InvalidProtection;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("nonbinary protection rejected"), Error.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid mask retains result"), Disabled == Before);
	Inputs.Protected = Protection;
	int32 CancellationChecks = 0;
	const auto CancelDuringSampling = [&CancellationChecks] {
		return ++CancellationChecks >= 5;
	};
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), CancelDuringSampling, Disabled);
	TestEqual(TEXT("in-flight cancellation reported"), Error.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("in-flight cancellation retains result"), Disabled == Before);
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), [] { return true; }, Disabled);
	TestEqual(TEXT("cancellation reported"), Error.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancel retains result"), Disabled == Before);
	Inputs.OriginCm.X = 1048576;
	Error = FVoxelLandM5FinalField::Build(Inputs, Budget.ToSharedRef(), Never, Disabled);
	TestEqual(TEXT("out of domain rejected"), Error.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("domain failure retains result"), Disabled == Before);
	return true;
}
#endif
