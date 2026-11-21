#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4PrepassIdentity.h"

namespace
{
constexpr EAutomationTestFlags M4PrepassIdentityFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4PrepassIdentityTest, "VoxelLand.M4.PrepassIdentity.Dependencies", M4PrepassIdentityFlags)
bool FVoxelLandM4PrepassIdentityTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bStable = false, bIgnoresDownstream = false;
	bool bSeedChanged = false, bCurveChanged = false, bProgramChanged = false, bDomainChanged = false;
	bool bInvalidPreserves = false, bErosionChanged = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		Draft.Constants = {0.25f};
		FVoxelLandInstruction Load, Store;
		Load.Op = EVoxelLandOpcode::LoadConstant;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program) || !Program)
		{
			return;
		}
		FVoxelLandM4WorldSnapshot S;
		S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
		S.Domain.bGlobalErosion = false;
		FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->CookedCurve.BakeVersion = 1;
			Channel->CookedCurve.SamplesQ8.Init(0, 4097);
		}
		TArray<uint8> Base, Other;
		auto Build = [&](TArray<uint8>& Out) {
			return VoxelLandM4PrepassIdentity::BuildCandidate(S, *Program, *Program, *Program, Out);
		};
		bSuccess = !Build(Base) && !Base.IsEmpty();
		bStable = !Build(Other) && Other == Base;
		S.bOverhangEnabled = true;
		S.OverhangAmplitudeQ8 = 128;
		S.Smooth.bEnabled = true;
		bIgnoresDownstream = !Build(Other) && Other == Base;
		++S.Seed;
		bSeedChanged = !Build(Other) && Other != Base;
		--S.Seed;
		S.Mountain.CookedCurve.SamplesQ8[2048] = 1;
		bCurveChanged = !Build(Other) && Other != Base;
		S.Mountain.CookedCurve.SamplesQ8[2048] = 0;
		++S.Domain.OriginXCm;
		bDomainChanged = !Build(Other) && Other != Base;
		--S.Domain.OriginXCm;
		Draft.Constants[0] = 0.5f;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Changed;
		if (!FVoxelLandProgram::Validate(Draft, Changed) && Changed)
		{
			bProgramChanged =
				!VoxelLandM4PrepassIdentity::BuildCandidate(S, *Program, *Changed, *Program, Other) && Other != Base;
		}
		S.Domain.bGlobalErosion = true;
		S.ErosionStageId = FGuid(1, 2, 3, 4);
		bErosionChanged = !Build(Other) && Other != Base;
		TArray<uint8> Sentinel = {42};
		S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		bInvalidPreserves = bool(Build(Sentinel)) && Sentinel.Num() == 1 && Sentinel[0] == 42;
	}).Get();
	TestTrue(TEXT("candidate can be built from validated programs"), bSuccess);
	TestTrue(TEXT("same inputs yield exact same bytes"), bStable);
	TestTrue(TEXT("downstream overhang and smoothing do not alter prepass identity"), bIgnoresDownstream);
	TestTrue(TEXT("seed changes identity"), bSeedChanged);
	TestTrue(TEXT("curve sample changes identity"), bCurveChanged);
	TestTrue(TEXT("control bytecode data changes identity"), bProgramChanged);
	TestTrue(TEXT("domain changes identity"), bDomainChanged);
	TestTrue(TEXT("erosion changes identity"), bErosionChanged);
	TestTrue(TEXT("unsupported domain fails without publication"), bInvalidPreserves);
	return true;
}

#endif
