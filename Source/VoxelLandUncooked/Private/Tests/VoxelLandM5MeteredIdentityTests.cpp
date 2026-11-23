#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM5MeteredIdentity.h"
#include "World/VoxelLandM5WorldIdentity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5MeteredIdentityTest, "VoxelLand.M5.Identity.MeteredFiniteDescriptor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5MeteredIdentityTest::RunTest(const FString& Parameters)
{
	bool bFixture = false;
	bool bEqual = false;
	bool bChanged = false;
	bool bInvalidAtomic = false;
	bool bMismatchAtomic = false;
	bool bPolicyAtomic = false;
	bool bDryIdentity = false, bDryMismatchAtomic = false;
	bool bCancelledAtomic = false;
	bool bBudgetAtomic = false;
	bool bHeld = false;
	bool bReleased = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM5WorldSnapshot Snapshot;
		Snapshot.Base.Domain.bGlobalErosion = false;
		Snapshot.Base.OverhangNodeId = FGuid(1, 2, 3, 4);
		Snapshot.Caves.bEnabled = true;
		Snapshot.Caves.StageId = FGuid(11, 22, 33, 44);
		Snapshot.Caves.Bounds.MinInclusive.X = Snapshot.Caves.Bounds.MinInclusive.Y =
			Snapshot.Caves.Bounds.MinInclusive.Z = -10000;
		Snapshot.Caves.Bounds.MaxExclusive.X = Snapshot.Caves.Bounds.MaxExclusive.Y =
			Snapshot.Caves.Bounds.MaxExclusive.Z = 10000;
		Snapshot.Caves.Limits.MaxValidationWork = 100000000;
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
		FVoxelLandM4ControlSnapshot* Channels[] = {
			&Snapshot.Base.Continental, &Snapshot.Base.Mountain, &Snapshot.Base.Peaks};
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
		FVoxelLandM5IdentityRecord Expected;
		if (!VoxelLandM5WorldIdentity::Capture(Snapshot, Expected, Error))
		{
			return;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, Budget))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Identity;
		const FVoxelLandM5Diagnostic Success =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		if (Success.Code != EVoxelLandM5ErrorCode::None || !Identity.IsValid())
		{
			Error = Success.Message;
			return;
		}
		bFixture = true;
		bEqual = Identity->GetIdentity().CanonicalBytes == Expected.CanonicalBytes
			  && Identity->GetIdentity().FingerprintBits == Expected.FingerprintBits
			  && Identity->GetIdentity().FormatVersion == Expected.FormatVersion;
		const FVoxelLandM5MeteredIdentity* Before = Identity.Get();
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		Snapshot.Manifest.Descriptor = 2;
		FVoxelLandM5IdentityRecord DryExpected;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> DryBudget;
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> DryIdentity;
		if (VoxelLandM5WorldIdentity::Capture(Snapshot, DryExpected, Error)
			&& FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, DryBudget))
		{
			const FVoxelLandM5Diagnostic DryResult = FVoxelLandM5MeteredIdentity::Capture(
				Snapshot, DryBudget.ToSharedRef(), [] { return false; }, DryIdentity);
			bDryIdentity = DryResult.Code == EVoxelLandM5ErrorCode::None && DryIdentity.IsValid()
				&& DryIdentity->GetIdentity().CanonicalBytes == DryExpected.CanonicalBytes
				&& DryExpected.CanonicalBytes != Expected.CanonicalBytes;
			const FVoxelLandM5MeteredIdentity* DryBefore = DryIdentity.Get();
			Snapshot.Manifest.Descriptor = 1;
			const FVoxelLandM5Diagnostic DryMismatch = FVoxelLandM5MeteredIdentity::Capture(
				Snapshot, DryBudget.ToSharedRef(), [] { return false; }, DryIdentity);
			bDryMismatchAtomic = DryBefore && DryMismatch.Code == EVoxelLandM5ErrorCode::InvalidInput
				&& DryIdentity.Get() == DryBefore;
		}
		Snapshot.Manifest.Descriptor = 1;
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::Ignore;
		Snapshot.Caves.Salt++;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ChangedBudget;
		if (!FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, ChangedBudget))
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Changed;
		const FVoxelLandM5Diagnostic ChangedResult =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, ChangedBudget.ToSharedRef(), [] { return false; }, Changed);
		bChanged = ChangedResult.Code == EVoxelLandM5ErrorCode::None && Changed.IsValid()
				&& Changed->GetIdentity().CanonicalBytes != Identity->GetIdentity().CanonicalBytes;
		Snapshot.Caves.Salt--;
		Snapshot.Manifest.Composition = 2;
		const FVoxelLandM5Diagnostic Invalid =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		bInvalidAtomic = Invalid.Code == EVoxelLandM5ErrorCode::InvalidInput && Identity.Get() == Before;
		Snapshot.Manifest.Composition = 1;
		Snapshot.Base.Continental.Payload.SourceFingerprint ^= 1;
		const FVoxelLandM5Diagnostic Mismatch =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		bMismatchAtomic = Mismatch.Code != EVoxelLandM5ErrorCode::None && Identity.Get() == Before;
		Snapshot.Base.Continental.Payload.SourceFingerprint ^= 1;
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::RejectFlooded;
		const FVoxelLandM5Diagnostic Water =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::Ignore;
		Snapshot.Caves.Protection.bRequireMask = true;
		const FVoxelLandM5Diagnostic Mask =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		Snapshot.Caves.Protection.bRequireMask = false;
		bPolicyAtomic = Water.Code == EVoxelLandM5ErrorCode::InvalidInput
					 && Mask.Code == EVoxelLandM5ErrorCode::InvalidInput && Identity.Get() == Before;
		const FVoxelLandM5Diagnostic Cancelled =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return true; }, Identity);
		bCancelledAtomic = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Identity.Get() == Before;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Empty;
		if (!FVoxelLandM5Budget::TryCreate(0, 0, Empty))
		{
			return;
		}
		const FVoxelLandM5Diagnostic Denied =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Empty.ToSharedRef(), [] { return false; }, Identity);
		bBudgetAtomic = Denied.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Identity.Get() == Before;
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Reader = Identity;
		Identity.Reset();
		int64 Live = -1;
		int64 Peak = 0;
		int64 Work = 0;
		Budget->GetUsage(Live, Peak, Work);
		bHeld = Live > 0;
		Reader.Reset();
		Budget->GetUsage(Live, Peak, Work);
		bReleased = Live == 0;
	}).Wait();
	TestTrue(FString::Printf(TEXT("fixture setup: %s"), *Error), bFixture);
	TestTrue(TEXT("exact canonical bytes and fingerprint match original"), bEqual);
	TestTrue(TEXT("changed descriptor changes bytes"), bChanged);
	TestTrue(TEXT("invalid version preserves output"), bInvalidAtomic);
	TestTrue(TEXT("mismatched M4 input preserves output"), bMismatchAtomic);
	TestTrue(TEXT("unimplemented water and masks reject atomically"), bPolicyAtomic);
	TestTrue(TEXT("metered dry identity matches unmetered identity"), bDryIdentity);
	TestTrue(TEXT("metered dry descriptor mismatch preserves output"), bDryMismatchAtomic);
	TestTrue(TEXT("cancellation preserves output"), bCancelledAtomic);
	TestTrue(TEXT("budget exhaustion preserves output"), bBudgetAtomic);
	TestTrue(TEXT("identity lease survives reader"), bHeld);
	TestTrue(TEXT("identity lease released"), bReleased);
	return bFixture;
}
#endif
