#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandM5MeteredM4Identity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredM4IdentityTest, "VoxelLand.M5.Identity.MeteredFiniteWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeteredM4IdentityTest::RunTest(const FString& Parameters)
{
	bool bFixture = false;
	bool bMatches = false;
	bool bInvalidAtomic = false;
	bool bCancelledAtomic = false;
	bool bBudgetAtomic = false;
	bool bMismatchAtomic = false;
	bool bLeaseHeld = false;
	bool bReleased = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Snapshot;
		Snapshot.Domain.bGlobalErosion = false;
		Snapshot.OverhangNodeId = FGuid(8, 7, 6, 5);
		FVoxelLandGraphNodeRecord Constant;
		FVoxelLandGraphNodeRecord Output;
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
		for (FVoxelLandM4ControlSnapshot* Channel : Channels)
		{
			Channel->Source.Nodes = {Constant, Output};
			Channel->Source.Links = {Link};
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(Channel->Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Program)
				|| !FVoxelLandProgramCodec::Pack(Channel->Source, *Program, Channel->Payload, Error))
			{
				return;
			}
			FVoxelLandM4CurveAuthoringKey First;
			FVoxelLandM4CurveAuthoringKey Last;
			Last.InputQ16 = 65535;
			Channel->CurveKeys = {First, Last};
			Channel->CookedCurve.SourceKeys = Channel->CurveKeys;
			Channel->CookedCurve.BakeVersion = 1;
			Channel->CookedCurve.SamplesQ8.Init(0, 4097);
			FCanonicalWriter Writer;
			for (int32 Value : Channel->CookedCurve.SamplesQ8)
			{
				Writer.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
			FMemory::Memcpy(&Channel->CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		FVoxelLandM4IdentityRecord Expected;
		if (!VoxelLandM4WorldIdentity::Capture(Snapshot, Expected, Error))
		{
			return;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, Budget))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Identity;
		const FVoxelLandM5Diagnostic Success =
			FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		if (Success.Code != EVoxelLandM5ErrorCode::None || !Identity.IsValid())
		{
			Error = Success.Message;
			return;
		}
		bFixture = true;
		bMatches = Identity->GetIdentity().CanonicalBytes == Expected.CanonicalBytes
				&& Identity->GetIdentity().FingerprintBits == Expected.FingerprintBits
				&& Identity->GetIdentity().FormatVersion == Expected.FormatVersion;
		const FVoxelLandM5MeteredM4Identity* Before = Identity.Get();
		Snapshot.Smooth.Version = 2;
		const FVoxelLandM5Diagnostic Invalid =
			FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		bInvalidAtomic = Invalid.Code == EVoxelLandM5ErrorCode::InvalidInput && Identity.Get() == Before;
		Snapshot.Smooth.Version = 1;
		Snapshot.Continental.Payload.SourceFingerprint ^= 1;
		const FVoxelLandM5Diagnostic Mismatch =
			FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		bMismatchAtomic = Mismatch.Code != EVoxelLandM5ErrorCode::None && Identity.Get() == Before;
		Snapshot.Continental.Payload.SourceFingerprint ^= 1;
		const FVoxelLandM5Diagnostic Cancelled =
			FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Budget.ToSharedRef(), [] { return true; }, Identity);
		bCancelledAtomic = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Identity.Get() == Before;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Empty;
		if (!FVoxelLandM5Budget::TryCreate(0, 0, Empty))
		{
			return;
		}
		const FVoxelLandM5Diagnostic Denied =
			FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Empty.ToSharedRef(), [] { return false; }, Identity);
		bBudgetAtomic = Denied.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Identity.Get() == Before;
		TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Reader = Identity;
		Identity.Reset();
		int64 Live = -1;
		int64 Peak = 0;
		int64 Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		bLeaseHeld = Live > 0;
		Reader.Reset();
		Budget->GetUsage(Live, Peak, Work);
		bReleased = Live == 0;
	}).Wait();
	TestTrue(FString::Printf(TEXT("fixture setup: %s"), *Error), bFixture);
	TestTrue(TEXT("metered identity matches original full bytes and hash"), bMatches);
	TestTrue(TEXT("invalid input preserves output"), bInvalidAtomic);
	TestTrue(TEXT("cancellation preserves output"), bCancelledAtomic);
	TestTrue(TEXT("budget exhaustion preserves output"), bBudgetAtomic);
	TestTrue(TEXT("mismatched payload preserves output"), bMismatchAtomic);
	TestTrue(TEXT("identity lease survives another reader"), bLeaseHeld);
	TestTrue(TEXT("identity lease is released"), bReleased);
	return bFixture;
}
#endif
