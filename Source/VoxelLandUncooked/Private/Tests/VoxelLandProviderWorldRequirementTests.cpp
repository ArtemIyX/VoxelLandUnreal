#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Operators/VoxelLandOperator.h"

namespace
{
class FWorldRequirementTestProvider final : public IVoxelLandOperator
{
public:
	FWorldRequirementTestProvider(const FGuid& InId, EVoxelLandWorldRequirement InRequirement,
		EVoxelLandOperatorStage InStage = EVoxelLandOperatorStage::Sample)
		: OperatorId(InId)
		, Requirement(InRequirement)
		, OperatorStage(InStage)
	{
	}

	FGuid Id() const override
	{
		++Calls;
		return OperatorId;
	}
	int32 Version() const override { return 1; }
	EVoxelLandOperatorStage Stage() const override { return OperatorStage; }
	EVoxelLandWorldRequirement WorldRequirement() const override { return Requirement; }
	TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
	EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
	uint32 MaximumWorkerBytes() const override { return 0; }
	bool ValidateConfiguration(TConstArrayView<uint8>, FString&) const override { return true; }
	TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8>, uint64, FString&) const override
	{
		return nullptr;
	}

	FGuid OperatorId;
	EVoxelLandWorldRequirement Requirement;
	EVoxelLandOperatorStage OperatorStage;
	mutable int32 Calls = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandProviderWorldRequirementTest,
	"VoxelLand.WorldModes.Provider.FrozenRequirement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandProviderWorldRequirementTest::RunTest(const FString& Parameters)
{
	const FGuid Id(0x847221ac, 0x11f04a15, 0x928e9d2b, 0x571b3a99);
	TSharedRef<FWorldRequirementTestProvider, ESPMode::ThreadSafe> Provider =
		MakeShared<FWorldRequirementTestProvider, ESPMode::ThreadSafe>(Id, EVoxelLandWorldRequirement::FiniteGlobal);
	FString Error;
	TestEqual(TEXT("Base provider requirement defaults to unspecified"),
		int32(Provider->IVoxelLandOperator::WorldRequirement()), int32(EVoxelLandWorldRequirement::Unspecified));
	const int32 InitialCalls = Provider->Calls;
	TestFalse(TEXT("Old ABI rejected"), FVoxelLandOperatorRegistry::RegisterV3(TEXT("VoxelLandUncooked"), Provider,
											FVoxelLandOperatorRegistry::ProviderAbiVersion - 1, Error));
	TestEqual(TEXT("ABI rejected before provider virtual calls"), Provider->Calls, InitialCalls);
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandUncooked"), Provider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
		AddError(Error);
		return false;
	}
	{
		const FVoxelLandOperatorRegistrySnapshot Frozen = FVoxelLandOperatorRegistry::Snapshot();
		TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Captured = Frozen.Resolve(Id, 1);
		TestTrue(TEXT("Snapshot contains registered provider"), Frozen.Num() >= 1);
		if (TestTrue(TEXT("Frozen provider resolves"), Captured.IsValid()))
		{
			TestEqual(TEXT("Finite-global declaration captured"), int32(Captured->WorldRequirement()),
				int32(EVoxelLandWorldRequirement::FiniteGlobal));
			// A mutable provider violates the interface contract. The immutable lease
			// must nevertheless retain its originally registered world requirement.
			Provider->Requirement = EVoxelLandWorldRequirement::SampleLocal;
			Provider->OperatorId = FGuid(1, 2, 3, 4);
			TestEqual(TEXT("Requirement remains frozen"), int32(Captured->WorldRequirement()),
				int32(EVoxelLandWorldRequirement::FiniteGlobal));
			TestTrue(TEXT("ID remains frozen"), Captured->Id() == Id);
			TestTrue(TEXT("Original ID remains resolvable"), Frozen.Resolve(Id, 1).IsValid());
			TestFalse(TEXT("Changed ID is not resolvable"), Frozen.Resolve(Provider->OperatorId, 1).IsValid());
			TestFalse(TEXT("Snapshot retains provider lease"), FVoxelLandOperatorRegistry::Unregister(Id, 1, Error));
		}
	}
	TestTrue(TEXT("Provider unregisters after frozen snapshot release"),
		FVoxelLandOperatorRegistry::Unregister(Id, 1, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandProviderWorldRequirementValidationTest,
	"VoxelLand.WorldModes.Provider.RequirementValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandProviderWorldRequirementValidationTest::RunTest(const FString& Parameters)
{
	FString Error;
	const FGuid InvalidId(0x847221ac, 0x11f04a15, 0x928e9d2b, 0x571b3a9a);
	auto Invalid = MakeShared<FWorldRequirementTestProvider, ESPMode::ThreadSafe>(
		InvalidId, static_cast<EVoxelLandWorldRequirement>(255));
	TestFalse(
		TEXT("Unknown requirement rejected"), FVoxelLandOperatorRegistry::RegisterV3(TEXT("VoxelLandUncooked"), Invalid,
												  FVoxelLandOperatorRegistry::ProviderAbiVersion, Error));
	const FGuid FilterId(0x847221ac, 0x11f04a15, 0x928e9d2b, 0x571b3a9b);
	auto Filter = MakeShared<FWorldRequirementTestProvider, ESPMode::ThreadSafe>(
		FilterId, EVoxelLandWorldRequirement::SampleLocal, EVoxelLandOperatorStage::OneCellFilter);
	TestFalse(TEXT("Filter cannot understate its neighborhood"),
		FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandUncooked"), Filter, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error));
	const FGuid UnknownId(0x847221ac, 0x11f04a15, 0x928e9d2b, 0x571b3a9c);
	auto Unknown = MakeShared<FWorldRequirementTestProvider, ESPMode::ThreadSafe>(
		UnknownId, EVoxelLandWorldRequirement::Unspecified);
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandUncooked"), Unknown, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
		AddError(Error);
		return false;
	}
	{
		const auto Snapshot = FVoxelLandOperatorRegistry::Snapshot();
		const auto Captured = Snapshot.Resolve(UnknownId, 1);
		if (TestTrue(TEXT("Unspecified provider resolves for editing"), Captured.IsValid()))
		{
			TestEqual(TEXT("Undeclared requirement stays unspecified"), int32(Captured->WorldRequirement()),
				int32(EVoxelLandWorldRequirement::Unspecified));
		}
	}
	TestTrue(TEXT("Unspecified provider unregisters"), FVoxelLandOperatorRegistry::Unregister(UnknownId, 1, Error));
	return true;
}
#endif
