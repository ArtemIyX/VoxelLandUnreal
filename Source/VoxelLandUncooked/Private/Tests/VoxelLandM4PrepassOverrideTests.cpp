#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Hash/xxhash.h"
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
	bool bInvalidRejected = false, bFiniteGolden = false, bFiniteWorldInputs = false;
	bool bInfiniteGolden = false, bInfiniteCandidateRejected = false;
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
		S.Seed = 1234;
		S.BaseHeightQ8 = 0;
		S.Domain.OriginXCm = -819200;
		S.Domain.OriginYCm = -819200;
		S.Domain.CellsX = 256;
		S.Domain.CellsY = 256;
		S.Domain.GridSpacingCm = 6400;
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
		auto Build = [&](TArray<uint8>& Bytes) {
			return VoxelLandM4PrepassIdentity::BuildCandidate(
				S, *UsedProgram, *ConstantProgram, *ConstantProgram, Bytes);
		};
		TArray<uint8> Base, Other;
		bBuilt = !Build(Base);
		// Fixed vectors were calculated separately from the documented little-endian fields,
		// including only the reachable continental override and three zero-filled curves.
		// Hashes use XXH3-64, as does Unreal Engine 5.7 FXxHash64.
		const TArray<uint8> FinitePrefix = {0x56, 0x4c, 0x50, 0x43, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0xd2, 0x04};
		bFiniteGolden = bBuilt && Base.Num() == 49569 && Base.Num() >= FinitePrefix.Num()
					 && FMemory::Memcmp(Base.GetData(), FinitePrefix.GetData(), FinitePrefix.Num()) == 0
					 && FXxHash64::HashBuffer(Base.GetData(), Base.Num()).Hash == 0x502df4ebbc9102ecULL;
		TArray<uint8> WorldInputs;
		bFiniteWorldInputs = !VoxelLandM4PrepassIdentity::BuildWorldInputs(
								 S, *UsedProgram, *ConstantProgram, *ConstantProgram, WorldInputs)
						  && WorldInputs == Base;
		S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		const TArray<uint8> InfinitePrefix = {0x56, 0x4c, 0x50, 0x43, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x01, 0xd2, 0x04};
		bInfiniteGolden = !VoxelLandM4PrepassIdentity::BuildWorldInputs(
							  S, *UsedProgram, *ConstantProgram, *ConstantProgram, WorldInputs)
					   && WorldInputs.Num() == 49537 && WorldInputs.Num() >= InfinitePrefix.Num()
					   && FMemory::Memcmp(WorldInputs.GetData(), InfinitePrefix.GetData(), InfinitePrefix.Num()) == 0
					   && FXxHash64::HashBuffer(WorldInputs.GetData(), WorldInputs.Num()).Hash == 0x6bbfe5bd055f0e5dULL;
		TArray<uint8> InfiniteSentinel = {42};
		bInfiniteCandidateRejected = bool(Build(InfiniteSentinel)) && InfiniteSentinel == TArray<uint8>({42});
		S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
		S.Continental.Overrides[1].FloatValue = 0.75f;
		bUnusedStable = !Build(Other) && Other == Base;
		S.Mountain.Overrides[0].FloatValue = 0.75f;
		bOtherChannelStable = !Build(Other) && Other == Base;
		S.Continental.Overrides[0].FloatValue = 0.75f;
		bUsedChanges = !Build(Other) && Other != Base;
		S.Continental.Overrides[1].ParameterId = FGuid(9, 9, 9, 9);
		TArray<uint8> Sentinel = {42};
		bInvalidRejected = bool(Build(Sentinel)) && Sentinel == TArray<uint8>({42});
	}).Get();
	TestTrue(TEXT("cache identity built"), bBuilt);
	TestTrue(TEXT("finite prepass matches independent byte and hash fixture"), bFiniteGolden);
	TestTrue(TEXT("finite world inputs equal finite prepass"), bFiniteWorldInputs);
	TestTrue(TEXT("infinite world inputs match independent byte and hash fixture"), bInfiniteGolden);
	TestTrue(TEXT("infinite world cannot produce a global prepass"), bInfiniteCandidateRejected);
	TestTrue(TEXT("unused override leaves cache identity unchanged"), bUnusedStable);
	TestTrue(TEXT("other projected channel ignores its unused override"), bOtherChannelStable);
	TestTrue(TEXT("reachable override changes cache identity"), bUsedChanges);
	TestTrue(TEXT("unknown unused override is rejected without publishing"), bInvalidRejected);
	return true;
}
#endif
