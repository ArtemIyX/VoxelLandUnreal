#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5DefinitionInputs.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Data/Structs/VoxelLandChunkKey.h"
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
	bool bDefaultInfluence = false, bExplicitInfluence = false, bInfluenceAtomic = false, bInfluenceDomain = false;
	bool bOwnerOnlyOverlap = false, bZeroRoofGate = false, bPocketColumns = false, bZeroPocket = false, bPocketSmooth = false;
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
		// Independent small-grid oracle: stored samples extend one lower and
		// one upper lattice step beyond the 32-cell chunk on each axis.
		auto RangeSnapshot = MakeShared<FVoxelLandM5WorldSnapshot, ESPMode::ThreadSafe>(Snapshot);
		RangeSnapshot->Base.Domain.OriginXCm = RangeSnapshot->Base.Domain.OriginYCm = -12800;
		RangeSnapshot->Base.Domain.CellsX = RangeSnapshot->Base.Domain.CellsY = 4;
		RangeSnapshot->Base.Domain.GridSpacingCm = 6400;
		RangeSnapshot->Caves.Bounds.MinInclusive.X = RangeSnapshot->Caves.Bounds.MinInclusive.Y = -12800;
		RangeSnapshot->Caves.Bounds.MaxExclusive.X = RangeSnapshot->Caves.Bounds.MaxExclusive.Y = 12801;
		RangeSnapshot->Caves.Bounds.MinInclusive.Z = -9000; RangeSnapshot->Caves.Bounds.MaxExclusive.Z = 11000;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> RangeBudget, OtherRangeBudget;
		if (FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, RangeBudget)
			&& FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 100000000, OtherRangeBudget))
		{
			FVoxelLandM5DefinitionInputs RangeInputs{RangeSnapshot, nullptr, RegistryOwner, RangeSnapshot->Caves.Bounds, false, true};
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> FullDefinition, SmallDefinition;
			const auto Full = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, FullDefinition);
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> FullSet;
			if (Full.Code == EVoxelLandM5ErrorCode::None && FullDefinition)
			{
				const auto FullResult = FVoxelLandM5InfluenceSet::BuildFinite(FullDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, FullSet);
				if (FullResult.Code == EVoxelLandM5ErrorCode::None && FullSet)
				{
					const auto& Gate = FullSet->GetDeclaredBounds();
					const auto& Read = FullSet->GetCoverage().ReadBoundsCm;
					bDefaultInfluence = Gate.MinInclusive.X == -9299 && Gate.MaxExclusive.X == 9300
						&& Gate.MinInclusive.Y == -9299 && Gate.MaxExclusive.Y == 9300
						&& Gate.MinInclusive.Z == -6099 && Gate.MaxExclusive.Z == 6100
						&& Read.MinInclusive.X == -11300 && Read.MaxExclusive.X == 11301
						&& Read.MinInclusive.Z == -8100 && Read.MaxExclusive.Z == 8101;
					for (int32 Z = -4; Z <= 3; ++Z) for (int32 Y = -5; Y <= 4; ++Y) for (int32 X = -5; X <= 4; ++X)
					{
						FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(X, Y, Z);
						const auto FitsXY = [](int32 K) { return int64(K) * 3200 - 1700 >= -12800 && int64(K) * 3200 + 4900 <= 12800; };
						const auto FitsZ = [](int32 K) { return int64(K) * 3200 - 1700 >= -9000 && int64(K) * 3200 + 4900 <= 10999; };
						bDefaultInfluence &= FullSet->CoversChunk(Key) == (FitsXY(X) && FitsXY(Y) && FitsZ(Z));
					}
					// One centimetre of growth on either side admits an owner
					// excluded by the independently checked read-valid range.
					bDefaultInfluence &= -4 * 3200 + 3500 == Gate.MinInclusive.X - 1
						&& 3 * 3200 - 300 == Gate.MaxExclusive.X;
				}
			}
			RangeSnapshot->Caves.RoofThicknessCm = 0;
			RangeSnapshot->Caves.Bounds.MinInclusive.Z = -12800; RangeSnapshot->Caves.Bounds.MaxExclusive.Z = 12801;
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> ZeroRoofDefinition;
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> ZeroRoofSet;
			const auto ZeroRoof = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, ZeroRoofDefinition);
			if (ZeroRoof.Code == EVoxelLandM5ErrorCode::None && ZeroRoofDefinition
				&& FVoxelLandM5InfluenceSet::BuildFinite(ZeroRoofDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, ZeroRoofSet).Code == EVoxelLandM5ErrorCode::None && ZeroRoofSet)
			{
				const auto& Gate = ZeroRoofSet->GetDeclaredBounds(); const auto& Read = ZeroRoofSet->GetCoverage().ReadBoundsCm;
				bZeroRoofGate = Gate.MinInclusive.X == -9299 && Gate.MaxExclusive.X == 9300
					&& Read.MinInclusive.X == -10300 && Read.MaxExclusive.X == 10301
					&& -4 * 3200 + 3500 == Gate.MinInclusive.X - 1 && 3 * 3200 - 300 == Gate.MaxExclusive.X;
				for (int32 K = -6; K <= 6; ++K)
				{
					FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(K, 0, 0);
					const bool Fits = int64(K) * 3200 - 700 >= -12800 && int64(K) * 3200 + 3900 <= 12800;
					bZeroRoofGate &= ZeroRoofSet->CoversChunk(Key) == Fits;
				}
			}
			RangeSnapshot->Caves.RoofThicknessCm = 1000;
			RangeSnapshot->Caves.Bounds.MinInclusive.Z = -9000; RangeSnapshot->Caves.Bounds.MaxExclusive.Z = 11000;
			RangeInputs.bExplicitBounds = true;
			RangeInputs.EffectiveBounds.MinInclusive.X = RangeInputs.EffectiveBounds.MinInclusive.Y = RangeInputs.EffectiveBounds.MinInclusive.Z = -10;
			RangeInputs.EffectiveBounds.MaxExclusive.X = RangeInputs.EffectiveBounds.MaxExclusive.Y = RangeInputs.EffectiveBounds.MaxExclusive.Z = 10;
			const auto Small = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, SmallDefinition);
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> SmallSet;
			if (Small.Code == EVoxelLandM5ErrorCode::None && SmallDefinition
				&& FVoxelLandM5InfluenceSet::BuildFinite(SmallDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, SmallSet).Code == EVoxelLandM5ErrorCode::None && SmallSet)
			{
				bExplicitInfluence = &SmallSet->GetDefinition() == SmallDefinition.Get() && SmallSet->UsesBudget(*RangeBudget);
				FVoxelLandChunkKey NonzeroLod; NonzeroLod.LOD = 1;
				bExplicitInfluence &= !SmallSet->CoversChunk(NonzeroLod);
				const auto PreviousInfluence = SmallSet;
				const auto Foreign = FVoxelLandM5InfluenceSet::BuildFinite(SmallDefinition.ToSharedRef(), OtherRangeBudget.ToSharedRef(), [] { return false; }, SmallSet);
				const auto CancelledInfluence = FVoxelLandM5InfluenceSet::BuildFinite(SmallDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return true; }, SmallSet);
				bInfluenceAtomic = Foreign.Code == EVoxelLandM5ErrorCode::InvalidInput && CancelledInfluence.Code == EVoxelLandM5ErrorCode::Cancelled && SmallSet == PreviousInfluence;
				for (int32 Z = -2; Z <= 1; ++Z) for (int32 Y = -2; Y <= 1; ++Y) for (int32 X = -2; X <= 1; ++X)
				{
					FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(X, Y, Z);
					// Direct owner-expanded interval overlap with [-10,10).
					const auto Overlap = [](int32 K) { return int64(K) * 3200 + 3500 >= -10 && int64(K) * 3200 - 300 < 10; };
					bExplicitInfluence &= SmallSet->CoversChunk(Key) == (Overlap(X) && Overlap(Y) && Overlap(Z));
				}
				RangeInputs.EffectiveBounds.MinInclusive.X = 3500;
				RangeInputs.EffectiveBounds.MaxExclusive.X = 3501;
				TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> PositiveDefinition;
				TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> PositiveSet;
				const auto Positive = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, PositiveDefinition);
				if (Positive.Code == EVoxelLandM5ErrorCode::None && PositiveDefinition
					&& FVoxelLandM5InfluenceSet::BuildFinite(PositiveDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, PositiveSet).Code == EVoxelLandM5ErrorCode::None && PositiveSet)
				{
					FVoxelLandChunkKey OwnerOnly; OwnerOnly.Coordinate = FIntVector(0, 0, 0);
					bOwnerOnlyOverlap = PositiveSet->CoversChunk(OwnerOnly);
				}
				RangeInputs.EffectiveBounds.MinInclusive.X = -3500;
				RangeInputs.EffectiveBounds.MaxExclusive.X = -3499;
				TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> NegativeDefinition;
				TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> NegativeSet;
				const auto Negative = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, NegativeDefinition);
				if (Negative.Code == EVoxelLandM5ErrorCode::None && NegativeDefinition
					&& FVoxelLandM5InfluenceSet::BuildFinite(NegativeDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, NegativeSet).Code == EVoxelLandM5ErrorCode::None && NegativeSet)
				{
					FVoxelLandChunkKey OwnerOnly; OwnerOnly.Coordinate = FIntVector(-1, 0, 0);
					bOwnerOnlyOverlap &= NegativeSet->CoversChunk(OwnerOnly);
				}
				else bOwnerOnlyOverlap = false;
				RangeInputs.EffectiveBounds.MinInclusive.X = -12800;
				RangeInputs.EffectiveBounds.MaxExclusive.X = -12700;
				TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> EdgeDefinition;
				const auto Edge = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, EdgeDefinition);
				const auto PriorSmall = SmallSet;
				if (Edge.Code == EVoxelLandM5ErrorCode::None && EdgeDefinition)
				{
					const auto EdgeResult = FVoxelLandM5InfluenceSet::BuildFinite(EdgeDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, SmallSet);
					RangeInputs.EffectiveBounds.MaxExclusive.X = MAX_int64;
					const auto PriorDefinition = EdgeDefinition;
					const auto Overflow = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, EdgeDefinition);
					bInfluenceDomain = EdgeResult.Code == EVoxelLandM5ErrorCode::OutOfDomain && SmallSet == PriorSmall
						&& Overflow.Code == EVoxelLandM5ErrorCode::OutOfDomain && EdgeDefinition == PriorDefinition;
				}
			}
			// A tall pocket column cannot be represented by the local roof halo.
			RangeSnapshot->Caves.Pockets.bEnabled = true;
			RangeSnapshot->Caves.Pockets.AmplitudeCm = 1;
			RangeSnapshot->Caves.Pockets.MinDepthCm = 0;
			RangeSnapshot->Caves.Pockets.MaxDepthCm = 50000;
			RangeSnapshot->Caves.Bounds.MinInclusive.Z = -100000;
			RangeSnapshot->Caves.Bounds.MaxExclusive.Z = 100001;
			RangeInputs.EffectiveBounds.MinInclusive.X = RangeInputs.EffectiveBounds.MinInclusive.Y = RangeInputs.EffectiveBounds.MinInclusive.Z = -10;
			RangeInputs.EffectiveBounds.MaxExclusive.X = RangeInputs.EffectiveBounds.MaxExclusive.Y = RangeInputs.EffectiveBounds.MaxExclusive.Z = 10;
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> PocketDefinition;
			const auto Pocket = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, PocketDefinition);
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> PocketSet;
			if (Pocket.Code == EVoxelLandM5ErrorCode::None && PocketDefinition)
			{
				const auto PocketResult = FVoxelLandM5InfluenceSet::BuildFinite(PocketDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, PocketSet);
				if (PocketResult.Code == EVoxelLandM5ErrorCode::None && PocketSet)
				{
					const auto& Coverage = PocketSet->GetCoverage();
					bPocketColumns = Coverage.bRequiresPocketColumns
						&& Coverage.PocketColumnReadBoundsCm.MinInclusive.Z == -100000
						&& Coverage.PocketColumnReadBoundsCm.MaxExclusive.Z == 100001
						&& Coverage.ReadBoundsCm.MinInclusive.Z > -100000 && Coverage.ReadBoundsCm.MaxExclusive.Z < 100001
						&& Coverage.PocketColumnBoundsCm.MinInclusive.X == -3700
						&& Coverage.PocketColumnBoundsCm.MaxExclusive.X == 3701
						&& Coverage.PocketColumnReadBoundsCm.MinInclusive.X == -3700
						&& Coverage.PocketColumnReadBoundsCm.MaxExclusive.X == 3701;
				}
			}
			RangeSnapshot->Base.Smooth.bEnabled = true;
			RangeSnapshot->Base.Smooth.Strength = 0.5f;
			RangeSnapshot->Base.bNormalizeSlope = true;
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> SmoothPocketDefinition;
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> SmoothPocketSet;
			const auto SmoothPocket = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, SmoothPocketDefinition);
			if (SmoothPocket.Code == EVoxelLandM5ErrorCode::None && SmoothPocketDefinition
				&& FVoxelLandM5InfluenceSet::BuildFinite(SmoothPocketDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, SmoothPocketSet).Code == EVoxelLandM5ErrorCode::None && SmoothPocketSet)
			{
				const auto& Coverage = SmoothPocketSet->GetCoverage();
				const auto& Column = Coverage.PocketColumnBoundsCm;
				const auto& Raw = Coverage.PocketColumnReadBoundsCm;
				bPocketSmooth = Column.MinInclusive.Z == -99900 && Column.MaxExclusive.Z == 99901
					&& Raw.MinInclusive.Z == -100000 && Raw.MaxExclusive.Z == 100001
					&& Column.MinInclusive.X == -3700 && Column.MaxExclusive.X == 3701
					&& Raw.MinInclusive.X == -3900 && Raw.MaxExclusive.X == 3901;
			}
			RangeSnapshot->Base.Smooth.bEnabled = false;
			RangeSnapshot->Base.bNormalizeSlope = false;
			RangeSnapshot->Caves.Pockets.AmplitudeCm = 0;
			TVoxelLandM5ConstPtr<FVoxelLandM5ValidatedDefinition> ZeroPocketDefinition;
			TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet> ZeroPocketSet;
			const auto ZeroPocket = FVoxelLandM5DefinitionValidator::Validate(RangeInputs, RangeBudget.ToSharedRef(), [] { return false; }, ZeroPocketDefinition);
			if (ZeroPocket.Code == EVoxelLandM5ErrorCode::None && ZeroPocketDefinition)
			{
				const auto Result = FVoxelLandM5InfluenceSet::BuildFinite(ZeroPocketDefinition.ToSharedRef(), RangeBudget.ToSharedRef(), [] { return false; }, ZeroPocketSet);
				bZeroPocket = Result.Code == EVoxelLandM5ErrorCode::None && ZeroPocketSet && !ZeroPocketSet->GetCoverage().bRequiresPocketColumns;
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
		Snapshot.Manifest.Composition = 1;
		const FVoxelLandM5Diagnostic Invalid =
			FVoxelLandM5MeteredIdentity::Capture(Snapshot, Budget.ToSharedRef(), [] { return false; }, Identity);
		bInvalidAtomic = Invalid.Code == EVoxelLandM5ErrorCode::InvalidInput && Identity.Get() == Before;
		Snapshot.Manifest.Composition = 2;
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
	TestTrue(TEXT("default maximal gate matches independent read-valid range"), bDefaultInfluence);
	TestTrue(TEXT("zero-roof gate and maximality match independent oracle"), bZeroRoofGate);
	TestTrue(TEXT("explicit gate includes every owner-overlap chunk"), bExplicitInfluence);
	TestTrue(TEXT("positive and negative owner-only overlaps are covered"), bOwnerOnlyOverlap);
	TestTrue(TEXT("foreign influence budget and cancellation preserve output"), bInfluenceAtomic);
	TestTrue(TEXT("boundary read and extreme declared bound reject atomically"), bInfluenceDomain);
	TestTrue(TEXT("active pocket records complete tall column separately"), bPocketColumns);
	TestTrue(TEXT("pocket column includes smoothing input halo"), bPocketSmooth);
	TestTrue(TEXT("zero-amplitude pocket requires no column"), bZeroPocket);
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
