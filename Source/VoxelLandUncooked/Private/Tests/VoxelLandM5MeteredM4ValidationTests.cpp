#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Data/Structs/VoxelLandM4CurveKey.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4CurveValidation.h"
#include "World/VoxelLandM4OverrideValidation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredM4ValidationTest, "VoxelLand.M5.Identity.MeteredM4Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5MeteredM4ValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ControlSnapshot Channel;
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	First.OutputQ8 = -500;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = -500;
	Channel.CurveKeys = {First, Last};
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	Channel.CookedCurve.BakeVersion = 1;
	Channel.CookedCurve.SamplesQ8.Init(-500, 4097);
	TArray<uint8> Bytes;
	for (int32 I = 0; I < 4097; ++I)
	{
		Bytes.Add(0x0c);
		Bytes.Add(0xfe);
		Bytes.Add(0xff);
		Bytes.Add(0xff);
	}
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	FMemory::Memcpy(&Channel.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
	FString Error;
	TestTrue(
		TEXT("analytic constant curve accepted by original"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5Budget::TryCreate(1000000, 10000000, Budget);
	if (!TestTrue(TEXT("budget created"), Budget.IsValid()))
	{
		return false;
	}
	auto Never = [] {
		return false;
	};
	TestEqual(TEXT("analytic constant curve accepted"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Budget.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::None);
	Channel.CookedCurve.SamplesQ8[17]++;
	TestEqual(TEXT("changed sample rejected"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Budget.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Channel.CookedCurve.SamplesQ8[17]--;
	TestEqual(TEXT("cancellation before bake"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Budget.ToSharedRef(), [] { return true; }).Code,
		EVoxelLandM5ErrorCode::Cancelled);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tight;
	FVoxelLandM5Budget::TryCreate(1, 10000000, Tight);
	TestEqual(TEXT("curve memory admission"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Tight.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	int64 Live = -1, Peak = -1, Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("curve scratch released"), Live, int64(0));

	FVoxelLandGraphParameter P;
	P.Id = FGuid(1, 2, 3, 4);
	P.Name = TEXT("Height");
	P.bHasRange = true;
	P.FloatMin = -1;
	P.FloatMax = 1;
	Channel.Source.Parameters.Add(P);
	FVoxelLandParameterOverride Override;
	Override.ParameterId = P.Id;
	Override.FloatValue = 1;
	Channel.Overrides.Add(Override);
	TestTrue(TEXT("old accepts inclusive override"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	TestEqual(TEXT("new accepts inclusive override"),
		VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::None);
	FVoxelLandGraphParameter Duplicate = P;
	Duplicate.Id = FGuid(5, 6, 7, 8);
	Duplicate.Name = TEXT("HEIGHT");
	Channel.Source.Parameters.Add(Duplicate);
	TestFalse(TEXT("old rejects duplicate folded name"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	TestEqual(TEXT("new rejects duplicate folded name"),
		VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Channel.Source.Parameters.Pop();
	Channel.Overrides.Add(Override);
	TestEqual(TEXT("duplicate override"),
		VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Empty;
	FVoxelLandM5Budget::TryCreate(0, 0, Empty);
	TestEqual(TEXT("override work admission"),
		VoxelLandM4OverrideValidation::ValidateMetered(Channel, Empty.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	int32 Polls = 0;
	TestEqual(TEXT("override cancels inside scan"),
		VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget.ToSharedRef(), [&Polls] { return ++Polls == 3; })
			.Code,
		EVoxelLandM5ErrorCode::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredM4BoundaryTest, "VoxelLand.M5.Identity.MeteredM4Boundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5MeteredM4BoundaryTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ControlSnapshot Channel;
	FVoxelLandM4CurveAuthoringKey A, B, C;
	A.InputQ16 = 0;
	A.OutputQ8 = -1;
	B.InputQ16 = 32768;
	B.OutputQ8 = -1;
	C.InputQ16 = 65535;
	C.OutputQ8 = 1;
	Channel.CurveKeys = {A, B, C};
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	Channel.CookedCurve.BakeVersion = 1;
	// Independent rational threshold oracle: the second segment rises from
	// -1 to +1. The nearest integers change at 1/4 and 3/4, with ties
	// rounded away from zero. All comparisons are exact integer inequalities.
	const int64 Start = int64(32768) * 4096;
	const int64 Width = int64(32767) * 4096;
	for (int64 I = 0; I < 4097; ++I)
	{
		const int64 X = I * 65535;
		const int64 Relative4 = 4 * (X - Start);
		Channel.CookedCurve.SamplesQ8.Add(X <= Start || Relative4 <= Width ? -1 : Relative4 >= 3 * Width ? 1 : 0);
	}
	auto Rehash = [&Channel] {
		TArray<uint8> Bytes;
		for (int32 V : Channel.CookedCurve.SamplesQ8)
		{
			const uint32 Bits = uint32(V);
			for (int32 BIndex = 0; BIndex < 4; ++BIndex)
			{
				Bytes.Add(uint8(Bits >> (8 * BIndex)));
			}
		}
		const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
		FMemory::Memcpy(&Channel.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
	};
	Rehash();
	const int64 RequiredWork = 128 * (3 + 4097 + 1);
	const int64 RequiredMemory = 4096 + 2 * 3 * sizeof(FVoxelLandM4CurveKey) + 4 * 4097 * sizeof(int32);
	auto Never = [] {
		return false;
	};
	auto MakeBudget = [](int64 Bytes, int64 Work) {
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Result;
		FVoxelLandM5Budget::TryCreate(Bytes, Work, Result);
		return Result;
	};
	auto Exact = MakeBudget(RequiredMemory, RequiredWork);
	TestEqual(TEXT("nonuniform rational curve at exact admission"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Exact.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::None);
	auto LowWork = MakeBudget(RequiredMemory, RequiredWork - 1);
	TestEqual(TEXT("one less curve work rejected"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, LowWork.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	auto LowMemory = MakeBudget(RequiredMemory - 1, RequiredWork);
	TestEqual(TEXT("one less curve byte rejected"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, LowMemory.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	int32 Polls = 0;
	auto Cancel = MakeBudget(RequiredMemory, RequiredWork);
	TestEqual(TEXT("post-batch cancellation wins"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, Cancel.ToSharedRef(), [&Polls] { return ++Polls == 3; })
			.Code,
		EVoxelLandM5ErrorCode::Cancelled);
	int64 Live = -1, Peak = -1, Work = -1;
	Cancel->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("post-batch cancellation releases lease"), Live, int64(0));
	const int32 OriginalSample = Channel.CookedCurve.SamplesQ8[3000];
	Channel.CookedCurve.SamplesQ8[3000] = 123;
	Rehash();
	auto StaleSample = MakeBudget(RequiredMemory, RequiredWork);
	TestEqual(TEXT("hash-correct stale sample rejected"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, StaleSample.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Channel.CookedCurve.SamplesQ8[3000] = OriginalSample;
	Rehash();
	Channel.CookedCurve.SourceKeys[1].OutputQ8++;
	auto StaleKeys = MakeBudget(RequiredMemory, RequiredWork);
	TestEqual(TEXT("stale source keys rejected even with valid sample hash"),
		VoxelLandM4CurveValidation::ValidateMetered(Channel, StaleKeys.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	// Endpoint-only ramps yield an exact half-tie at sample 2048:
	// i/4096 is exactly 1/2, independently of the 65535 key scale.
	for (int32 Direction = 0; Direction != 2; ++Direction)
	{
		FVoxelLandM4CurveAuthoringKey Left, Right;
		Left.InputQ16 = 0;
		Right.InputQ16 = 65535;
		Left.OutputQ8 = Direction == 0 ? -1 : 0;
		Right.OutputQ8 = Direction == 0 ? 0 : 1;
		Channel.CurveKeys = {Left, Right};
		Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
		for (int32 I = 0; I < 4097; ++I)
		{
			Channel.CookedCurve.SamplesQ8[I] = Direction == 0 ? (I <= 2048 ? -1 : 0) : (I < 2048 ? 0 : 1);
		}
		Rehash();
		TestEqual(Direction == 0 ? TEXT("negative half tie expected") : TEXT("positive half tie expected"),
			Channel.CookedCurve.SamplesQ8[2048], Direction == 0 ? -1 : 1);
		auto TieBudget = MakeBudget(RequiredMemory, RequiredWork);
		TestEqual(Direction == 0 ? TEXT("negative half tie validates") : TEXT("positive half tie validates"),
			VoxelLandM4CurveValidation::ValidateMetered(Channel, TieBudget.ToSharedRef(), Never).Code,
			EVoxelLandM5ErrorCode::None);
	}

	FVoxelLandM4ControlSnapshot Overrides;
	FVoxelLandGraphParameter P;
	P.Id = FGuid(9, 8, 7, 6);
	P.Name = TEXT("Value");
	P.Type = EVoxelLandGraphValueType::Int32;
	P.bHasRange = true;
	P.IntMin = -2;
	P.IntMax = 2;
	Overrides.Source.Parameters.Add(P);
	FVoxelLandParameterOverride O;
	O.ParameterId = P.Id;
	O.Type = P.Type;
	O.IntValue = -2;
	Overrides.Overrides.Add(O);
	// Initial + five name characters + lookup + final = eight work units.
	auto ExactOverride = MakeBudget(0, 8);
	TestEqual(TEXT("inclusive int min and exact work"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, ExactOverride.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::None);
	auto LowOverride = MakeBudget(0, 7);
	TestEqual(TEXT("one less override work"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, LowOverride.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	Overrides.Overrides[0].IntValue = -3;
	auto More = MakeBudget(0, 10000);
	TestEqual(TEXT("int range rejected"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Overrides.Source.Parameters[0].Type = EVoxelLandGraphValueType::Bool;
	Overrides.Overrides[0].Type = EVoxelLandGraphValueType::Bool;
	TestEqual(TEXT("bool range rejected"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Overrides.Source.Parameters[0].bHasRange = false;
	TestEqual(TEXT("bool without range accepted"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::None);
	Overrides.Overrides[0].ParameterId = FGuid(42, 0, 0, 0);
	TestEqual(TEXT("missing ID rejected"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Overrides.Overrides[0].ParameterId = P.Id;
	Overrides.Source.Parameters.Add(Overrides.Source.Parameters[0]);
	TestEqual(TEXT("duplicate ID rejected"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Overrides.Source.Parameters.Pop();
	Overrides.Source.Parameters[0].Name = TEXT("1invalid");
	TestEqual(TEXT("leading digit rejected"),
		VoxelLandM4OverrideValidation::ValidateMetered(Overrides, More.ToSharedRef(), Never).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	return true;
}
#endif
