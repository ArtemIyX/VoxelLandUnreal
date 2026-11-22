#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4PrepassIdentity.h"

namespace
{
constexpr EAutomationTestFlags M4PrepassOverrideFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4PrepassOverrideTest, "VoxelLand.M4.PrepassIdentity.ReachableOverrides", M4PrepassOverrideFlags)
bool FVoxelLandM4PrepassOverrideTest::RunTest(const FString& Parameters)
{
	bool bBuilt = false, bUnusedStable = false, bUsedChanges = false, bOtherChannelStable = false;
	bool bInvalidRejected = false, bLegacyChanges = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandParameterSlot Slot;
		Slot.Name = TEXT("Used");
		Draft.Parameters.Add(Slot);
		FVoxelLandInstruction Load, Store;
		Load.Op = EVoxelLandOpcode::LoadFloatParameter;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> UsedProgram;
		if (FVoxelLandProgram::Validate(Draft, UsedProgram) || !UsedProgram)
		{
			return;
		}
		Draft.Parameters.Reset();
		Draft.Constants = {0.f};
		Draft.Instructions[0].Op = EVoxelLandOpcode::LoadConstant;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> ConstantProgram;
		if (FVoxelLandProgram::Validate(Draft, ConstantProgram) || !ConstantProgram)
		{
			return;
		}
		FVoxelLandM4WorldSnapshot S;
		S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
		S.Domain.bGlobalErosion = false;
		FVoxelLandGraphParameter Used, Unused;
		Used.Id = FGuid(1, 2, 3, 4);
		Used.Name = TEXT("Used");
		Unused.Id = FGuid(2, 2, 3, 4);
		Unused.Name = TEXT("Unused");
		FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source.Parameters = {Used, Unused};
			Channel->CookedCurve.BakeVersion = 1;
			Channel->CookedCurve.SamplesQ8.Init(0, 4097);
		}
		FVoxelLandSerializedParameter Compiled;
		Compiled.Id = Used.Id;
		Compiled.Name = Used.Name;
		S.Continental.Payload.Parameters.Add(Compiled);
		FVoxelLandParameterOverride A, B;
		A.ParameterId = Used.Id;
		A.FloatValue = 0.25f;
		B.ParameterId = Unused.Id;
		B.FloatValue = 0.5f;
		S.Continental.Overrides = {A, B};
		S.Mountain.Overrides = {A};
		auto Build = [&](TArray<uint8>& Bytes, uint32 Version) {
			return VoxelLandM4PrepassIdentity::BuildCandidate(
				S, *UsedProgram, *ConstantProgram, *ConstantProgram, Bytes, Version);
		};
		TArray<uint8> Base, Other, Legacy;
		bBuilt = !Build(Base, 4) && !Build(Legacy, 2);
		S.Continental.Overrides[1].FloatValue = 0.75f;
		bUnusedStable = !Build(Other, 4) && Other == Base;
		bLegacyChanges = !Build(Other, 2) && Other != Legacy;
		S.Mountain.Overrides[0].FloatValue = 0.75f;
		bOtherChannelStable = !Build(Other, 4) && Other == Base;
		S.Continental.Overrides[0].FloatValue = 0.75f;
		bUsedChanges = !Build(Other, 4) && Other != Base;
		S.Continental.Overrides[1].ParameterId = FGuid(9, 9, 9, 9);
		TArray<uint8> Sentinel = {42};
		bInvalidRejected = bool(Build(Sentinel, 4)) && Sentinel == TArray<uint8>({42});
	}).Get();
	TestTrue(TEXT("cache identity built"), bBuilt);
	TestTrue(TEXT("unused override leaves cache identity unchanged"), bUnusedStable);
	TestTrue(TEXT("other projected channel ignores its unused override"), bOtherChannelStable);
	TestTrue(TEXT("reachable override changes cache identity"), bUsedChanges);
	TestTrue(TEXT("legacy encoding retains authored overrides"), bLegacyChanges);
	TestTrue(TEXT("unknown unused override is rejected without publishing"), bInvalidRejected);
	return true;
}
#endif
