#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Field/Chunks/VoxelLandGeneratorSettings.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "World/Caves/Planning/VoxelLandCaveFiniteControls.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteControlsTest, "VoxelLand.M5.FiniteControls.Assembly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteControlsTest::RunTest(const FString& Parameters)
{
	bool bBuilt = false, bIdentity = false, bPrograms = false, bCurves = false;
	bool bInvalid = false, bCancelled = false, bBudget = false, bRetained = false, bReleased = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Snapshot;
		Snapshot.Domain.bGlobalErosion = false;
		Snapshot.OverhangNodeId = FGuid(8, 7, 6, 5);
		FVoxelLandGraphNodeRecord Constant, Output;
		Constant.Id = FGuid(1, 1, 1, 1);
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Constant.Pins.SetNum(2);
		Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
		Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
		Constant.Pins[1].bOutput = true;
		Output.Id = FGuid(1, 1, 1, 2);
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = FGuid(2, 1, 1, 3);
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Constant.Id;
		Link.SourcePin = Constant.Pins[1].Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
		FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		uint64 Fingerprints[3] = {};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVoxelLandM4ControlSnapshot& Channel = *Channels[Index];
			Channel.Source.Nodes = {Constant, Output};
			Channel.Source.Links = {Link};
			// An authored but unreachable declaration has no compiled slot and
			// must not become a generator override.
			FVoxelLandGraphParameter Unused;
			Unused.Id = FGuid(7, 6, 5, 4);
			Unused.Name = TEXT("Unused");
			Channel.Source.Parameters.Add(Unused);
			FVoxelLandParameterOverride Override;
			Override.ParameterId = Unused.Id;
			Override.FloatValue = 0.75f;
			Channel.Overrides.Add(Override);
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(Channel.Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Program) || !Program
				|| !FVoxelLandProgramCodec::Pack(Channel.Source, *Program, Channel.Payload, Error))
			{
				return;
			}
			Fingerprints[Index] = Program->GetFingerprint();
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			Channel.CurveKeys = {First, Last};
			Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
			Channel.CookedCurve.BakeVersion = 1;
			Channel.CookedCurve.SamplesQ8.Init(0, FVoxelLandM4CurveLut::SampleCount);
			FCanonicalWriter Writer;
			for (int32 Value : Channel.CookedCurve.SamplesQ8)
			{
				Writer.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
			FMemory::Memcpy(&Channel.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		FVoxelLandM4IdentityRecord Expected;
		if (!VoxelLandM4WorldIdentity::Capture(Snapshot, Expected, Error))
		{
			return;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Meter;
		if (!FVoxelLandM5Budget::TryCreate(128ll * 1024 * 1024, 100000000, Meter))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5FiniteControls, ESPMode::ThreadSafe> Controls;
		const auto Never = [] {
			return false;
		};
		auto Diagnostic = FVoxelLandM5FiniteControls::Build(Snapshot, Meter.ToSharedRef(), Never, Controls);
		if (Diagnostic.Code != EVoxelLandM5ErrorCode::None || !Controls)
		{
			Error = Diagnostic.Message;
			return;
		}
		bBuilt = true;
		bIdentity = Controls->GetIdentity().CanonicalBytes == Expected.CanonicalBytes
				 && Controls->GetIdentity().FingerprintBits == Expected.FingerprintBits;
		bPrograms = true;
		bCurves = true;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			bPrograms &= Controls->GetGenerator(Index).GetProgram().GetFingerprint() == Fingerprints[Index];
			bPrograms &= Controls->GetGenerator(Index).GetProfile().VoxelSizeCm == float(Snapshot.VoxelSizeCm);
			bPrograms &= Controls->GetGenerator(Index).GetProfile().FloatOverrides.IsEmpty();
			bCurves &= Controls->GetCurve(Index).Evaluate(32768) == 0;
		}
		const FVoxelLandM5FiniteControls* Previous = Controls.Get();
		Snapshot.Smooth.Version = 2;
		Diagnostic = FVoxelLandM5FiniteControls::Build(Snapshot, Meter.ToSharedRef(), Never, Controls);
		bInvalid = Diagnostic.Code != EVoxelLandM5ErrorCode::None && Controls.Get() == Previous;
		Snapshot.Smooth.Version = 1;
		Diagnostic = FVoxelLandM5FiniteControls::Build(Snapshot, Meter.ToSharedRef(), [] { return true; }, Controls);
		bCancelled = Diagnostic.Code == EVoxelLandM5ErrorCode::Cancelled && Controls.Get() == Previous;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Empty;
		if (!FVoxelLandM5Budget::TryCreate(0, 0, Empty))
		{
			return;
		}
		Diagnostic = FVoxelLandM5FiniteControls::Build(Snapshot, Empty.ToSharedRef(), Never, Controls);
		bBudget = Diagnostic.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Controls.Get() == Previous;
		int64 Live = 0, Peak = 0, Work = 0;
		Meter->GetUsage(Live, Peak, Work);
		bRetained = Live > 0;
		Controls.Reset();
		Meter->GetUsage(Live, Peak, Work);
		bReleased = Live == 0;
	}).Wait();
	TestTrue(FString::Printf(TEXT("fixture setup: %s"), *Error), bBuilt);
	TestTrue(TEXT("full M4 identity is retained"), bIdentity);
	TestTrue(TEXT("three matching programs and profiles"), bPrograms);
	TestTrue(TEXT("three curves are retained"), bCurves);
	TestTrue(TEXT("invalid snapshot preserves output"), bInvalid);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("budget exhaustion preserves output"), bBudget);
	TestTrue(TEXT("live controls retain admission"), bRetained);
	TestTrue(TEXT("admission released with controls"), bReleased);
	return bBuilt;
}
#endif
