#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Operators/VoxelLandOperator.h"
#include "World/VoxelLandM5DefinitionValidator.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
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
	bool bDefinition = false, bDefinitionBounds = false, bDefinitionAtomic = false, bDefinitionDefault = false;
	bool bDefinitionFrozen = false, bTooltipHistory = false;
	bool bMaskIdentity = false, bMaskCellChanged = false, bMaskGeometryChanged = false;
	bool bMaskEncoding = false;
	bool bMaskMissing = false, bMaskForeign = false, bMaskWater = false, bMaskCancelled = false;
	bool bCancelledAtomic = false;
	bool bBudgetAtomic = false;
	bool bHeld = false;
	bool bReleased = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM5WorldSnapshot Presentation;
		auto& ParameterText = Presentation.Base.Continental.Source.Parameters.AddDefaulted_GetRef().Tooltip;
		auto& FunctionText = Presentation.Base.Continental.Source.Functions.AddDefaulted_GetRef().Inputs.AddDefaulted_GetRef().Tooltip;
		ParameterText = FText::Format(FText::FromString(TEXT("{0}")), FText::GetEmpty());
		FunctionText = FText::Format(FText::FromString(TEXT("{0}")), FText::GetEmpty());
		const bool bHistoryExists = ParameterText.IsEmpty() && FunctionText.IsEmpty()
			&& !ParameterText.IdenticalTo(FText::GetEmpty()) && !FunctionText.IdenticalTo(FText::GetEmpty());
		FVoxelLandM5DefinitionValidator::TestSanitizeTooltips(Presentation);
		bTooltipHistory = bHistoryExists && ParameterText.IdenticalTo(FText::GetEmpty()) && FunctionText.IdenticalTo(FText::GetEmpty());
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
			FVoxelLandGraphParameter Parameter;
			Parameter.Id = FGuid(9, 8, 7, 6); Parameter.Name = TEXT("Unused");
			// Empty display text with a nontrivial format history.
			Parameter.Tooltip = FText::Format(FText::FromString(TEXT("{0}")), FText::GetEmpty());
			Channel->Source.Parameters.Add(Parameter);
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
		// Native explicit finite definition binds complete descriptor and declared bounds.
		Snapshot.Manifest.Descriptor = 2;
		auto SnapshotOwner = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Snapshot);
		auto RegistryOwner = MakeShared<FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe>(FVoxelLandOperatorRegistry::Snapshot());
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> DefinitionBudget;
		if (FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, DefinitionBudget))
		{
			FVoxelLandM5DefinitionInputs Inputs{SnapshotOwner, nullptr, RegistryOwner, Snapshot.Caves.Bounds, true, true};
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> Definition;
			const auto Valid = FVoxelLandM5DefinitionValidator::Validate(Inputs, DefinitionBudget.ToSharedRef(), [] { return false; }, Definition);
			bDefinition = Valid.Code == EVoxelLandM5ErrorCode::None && Definition
				&& &Definition->GetSnapshot() != &SnapshotOwner.Get() && &Definition->GetRegistry() != &RegistryOwner.Get()
				&& Definition->UsesBudget(*DefinitionBudget) && Definition->GetCanonicalBytes().Num() > DryExpected.CanonicalBytes.Num();
			if (Definition)
			{
				const auto BeforeDefinition = Definition;
				const int64 OriginalSeed = Definition->GetSnapshot().Base.Seed;
				SnapshotOwner->Base.Seed ^= 1;
				bDefinitionFrozen = Definition->GetSnapshot().Base.Seed == OriginalSeed
					&& SnapshotOwner->Base.Continental.Source.Parameters[0].Tooltip.IsEmpty()
					&& !SnapshotOwner->Base.Continental.Source.Parameters[0].Tooltip.IdenticalTo(FText::GetEmpty())
					&& Definition->GetSnapshot().Base.Continental.Source.Parameters[0].Tooltip.IdenticalTo(FText::GetEmpty());
				SnapshotOwner->Base.Seed ^= 1;
				Inputs.bAuthoritativeDryWorld = false;
				const auto Dry = FVoxelLandM5DefinitionValidator::Validate(Inputs, DefinitionBudget.ToSharedRef(), [] { return false; }, Definition);
				Inputs.bAuthoritativeDryWorld = true;
				const auto Cancel = FVoxelLandM5DefinitionValidator::Validate(Inputs, DefinitionBudget.ToSharedRef(), [] { return true; }, Definition);
				bDefinitionAtomic = Dry.Code == EVoxelLandM5ErrorCode::InvalidInput && Cancel.Code == EVoxelLandM5ErrorCode::Cancelled && Definition == BeforeDefinition;
				Inputs.bExplicitBounds = false;
				const auto Default = FVoxelLandM5DefinitionValidator::Validate(Inputs, DefinitionBudget.ToSharedRef(), [] { return false; }, Definition);
				bDefinitionDefault = Default.Code == EVoxelLandM5ErrorCode::Unproven && Definition == BeforeDefinition;
				Inputs.bExplicitBounds = true;
				++Inputs.EffectiveBounds.MinInclusive.X;
				const auto Changed = FVoxelLandM5DefinitionValidator::Validate(Inputs, DefinitionBudget.ToSharedRef(), [] { return false; }, Definition);
				bDefinitionBounds = Changed.Code == EVoxelLandM5ErrorCode::None && Definition
					&& Definition->GetCanonicalBytes().Num() == BeforeDefinition->GetCanonicalBytes().Num()
					&& FMemory::Memcmp(Definition->GetCanonicalBytes().GetData(), BeforeDefinition->GetCanonicalBytes().GetData(),
						Definition->GetCanonicalBytes().Num()) != 0;
			}
		}
		// Descriptor 3 retains the full mask rather than only its hash.
		Snapshot.Manifest.Descriptor = 3;
		Snapshot.Caves.Protection.bRequireMask = true;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> MaskBudget;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ForeignBudget;
		if (!FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, MaskBudget)
			|| !FVoxelLandM5Budget::TryCreate(64ll * 1024 * 1024, 100000000, ForeignBudget))
		{
			return;
		}
		TArray<uint8> Cells;
		Cells.Init(0, 8);
		Cells[1] = Cells[4] = Cells[6] = 1;
		FVoxelLandM5PointCm MaskOrigin;
		MaskOrigin.X = -100;
		MaskOrigin.Y = -257;
		MaskOrigin.Z = 42;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Grid;
		if (FVoxelLandM5ProtectionGrid::Capture(FIntVector(2, 2, 2), MaskOrigin, 100, Cells,
				MaskBudget.ToSharedRef(), [] { return false; }, Grid).Code != EVoxelLandM5ErrorCode::None)
		{
			return;
		}
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> MaskIdentity;
		const auto MaskOk = FVoxelLandM5MeteredIdentity::Capture(
			Snapshot, *Grid, MaskBudget.ToSharedRef(), [] { return false; }, MaskIdentity);
		bMaskIdentity = MaskOk.Code == EVoxelLandM5ErrorCode::None && MaskIdentity.IsValid()
			&& MaskIdentity->GetIdentity().CanonicalBytes != DryExpected.CanonicalBytes;
		// Independent literal suffix: 2x2x2, origin (-100,-257,42), spacing
		// 100, eight cells [0,1,0,0,1,0,1,0]. Signed values use 64-bit
		// two's-complement, least significant byte first. Descriptor 2 has the
		// same fixed-length prefix, so its length is the exact suffix offset.
		const uint8 ExpectedMaskSuffix[] = {
			0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
			0x9c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00};
		if (MaskIdentity.IsValid())
		{
			const TArray<uint8>& Bytes = MaskIdentity->GetIdentity().CanonicalBytes;
			const int32 Offset = DryExpected.CanonicalBytes.Num();
			bMaskEncoding = Bytes.Num() == Offset + UE_ARRAY_COUNT(ExpectedMaskSuffix)
				&& FMemory::Memcmp(Bytes.GetData() + Offset, ExpectedMaskSuffix, sizeof(ExpectedMaskSuffix)) == 0;
		}
		const FVoxelLandM5MeteredIdentity* MaskBefore = MaskIdentity.Get();
		const auto Missing = FVoxelLandM5MeteredIdentity::Capture(
			Snapshot, MaskBudget.ToSharedRef(), [] { return false; }, MaskIdentity);
		bMaskMissing = MaskBefore && Missing.Code == EVoxelLandM5ErrorCode::InvalidInput && MaskIdentity.Get() == MaskBefore;
		const auto Foreign = FVoxelLandM5MeteredIdentity::Capture(
			Snapshot, *Grid, ForeignBudget.ToSharedRef(), [] { return false; }, MaskIdentity);
		bMaskForeign = Foreign.Code == EVoxelLandM5ErrorCode::InvalidInput && MaskIdentity.Get() == MaskBefore;
		const auto CancelMask = FVoxelLandM5MeteredIdentity::Capture(
			Snapshot, *Grid, MaskBudget.ToSharedRef(), [] { return true; }, MaskIdentity);
		bMaskCancelled = CancelMask.Code == EVoxelLandM5ErrorCode::Cancelled && MaskIdentity.Get() == MaskBefore;
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::Ignore;
		const auto WrongWater = FVoxelLandM5MeteredIdentity::Capture(
			Snapshot, *Grid, MaskBudget.ToSharedRef(), [] { return false; }, MaskIdentity);
		bMaskWater = WrongWater.Code == EVoxelLandM5ErrorCode::InvalidInput && MaskIdentity.Get() == MaskBefore;
		Snapshot.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		Cells[7] = 1;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ChangedGrid;
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> MaskChanged;
		if (FVoxelLandM5ProtectionGrid::Capture(FIntVector(2, 2, 2), MaskOrigin, 100, Cells,
				MaskBudget.ToSharedRef(), [] { return false; }, ChangedGrid).Code == EVoxelLandM5ErrorCode::None)
		{
			const auto ChangedMask = FVoxelLandM5MeteredIdentity::Capture(
				Snapshot, *ChangedGrid, MaskBudget.ToSharedRef(), [] { return false; }, MaskChanged);
			bMaskCellChanged = MaskBefore && ChangedMask.Code == EVoxelLandM5ErrorCode::None && MaskChanged.IsValid()
				&& MaskChanged->GetIdentity().CanonicalBytes != MaskIdentity->GetIdentity().CanonicalBytes;
		}
		Cells[7] = 0;
		MaskOrigin.X = -200;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ShiftedGrid;
		TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> MaskShifted;
		if (FVoxelLandM5ProtectionGrid::Capture(FIntVector(2, 2, 2), MaskOrigin, 100, Cells,
				MaskBudget.ToSharedRef(), [] { return false; }, ShiftedGrid).Code == EVoxelLandM5ErrorCode::None)
		{
			const auto ShiftedMask = FVoxelLandM5MeteredIdentity::Capture(
				Snapshot, *ShiftedGrid, MaskBudget.ToSharedRef(), [] { return false; }, MaskShifted);
			bMaskGeometryChanged = MaskBefore && ShiftedMask.Code == EVoxelLandM5ErrorCode::None && MaskShifted.IsValid()
				&& MaskShifted->GetIdentity().CanonicalBytes != MaskIdentity->GetIdentity().CanonicalBytes;
		}
		Snapshot.Caves.Protection.bRequireMask = false;
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
	TestTrue(TEXT("native finite definition retains source owners and canonical descriptor"), bDefinition);
	TestTrue(TEXT("declared effective bounds change full identity"), bDefinitionBounds);
	TestTrue(TEXT("definition snapshot is independent of mutable input alias"), bDefinitionFrozen);
	TestTrue(TEXT("parameter and function-input empty-display histories are canonicalized"), bTooltipHistory);
	TestTrue(TEXT("definition rejects missing dry declaration and cancellation atomically"), bDefinitionAtomic);
	TestTrue(TEXT("unproved default domain rejects atomically"), bDefinitionDefault);
	TestTrue(TEXT("dry mask descriptor has an identity"), bMaskIdentity);
	TestTrue(TEXT("mask descriptor uses the independent canonical suffix"), bMaskEncoding);
	TestTrue(TEXT("mask cell changes identity"), bMaskCellChanged);
	TestTrue(TEXT("mask origin changes identity"), bMaskGeometryChanged);
	TestTrue(TEXT("missing mask rejects atomically"), bMaskMissing);
	TestTrue(TEXT("foreign mask budget rejects atomically"), bMaskForeign);
	TestTrue(TEXT("mask requires dry policy"), bMaskWater);
	TestTrue(TEXT("mask identity cancellation preserves output"), bMaskCancelled);
	TestTrue(TEXT("cancellation preserves output"), bCancelledAtomic);
	TestTrue(TEXT("budget exhaustion preserves output"), bBudgetAtomic);
	TestTrue(TEXT("identity lease survives reader"), bHeld);
	TestTrue(TEXT("identity lease released"), bReleased);
	return bFixture;
}
#endif
