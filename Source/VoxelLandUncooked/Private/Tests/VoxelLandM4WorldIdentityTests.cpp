#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandM4PrepassIdentity.h"
#include "UObject/Class.h"

namespace
{
constexpr EAutomationTestFlags M4WorldIdentityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4WorldIdentityTest, "VoxelLand.M4.WorldIdentity.Matches", M4WorldIdentityTestFlags)
bool FVoxelLandM4WorldIdentityTest::RunTest(const FString& Parameters)
{
	bool bBuilt = false, bStable = false, bSeed = false, bShape = false, bDetail = false;
	bool bMaterial = false, bUnlimited = false, bVersion = false, bCorrupt = false, bRoundTrip = false;
	bool bInvalidOverride = false, bLegacyExact = false, bLegacyMatch = false;
	bool bInvalidBake = false, bFixedIdentity = false;
	bool bCanonicalReordered = false, bLegacyOrderSensitive = false;
	bool bSourceBound = false, bV2Compatible = false, bPrepassReused = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot S;
		S.Domain.bGlobalErosion = false;
		S.OverhangNodeId = FGuid(1, 2, 3, 4);
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
		FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
		for (FVoxelLandM4ControlSnapshot* C : Channels)
		{
			C->Source.Nodes = {Constant, Output};
			C->Source.Links = {Link};
			if (C == &S.Continental)
			{
				FVoxelLandGraphParameter FirstParameter, SecondParameter;
				FirstParameter.Id = FGuid(1, 0, 0, 0);
				FirstParameter.Name = TEXT("First");
				SecondParameter.Id = FGuid(2, 0, 0, 0);
				SecondParameter.Name = TEXT("Second");
				C->Source.Parameters = {FirstParameter, SecondParameter};
				FVoxelLandParameterOverride FirstOverride, SecondOverride;
				FirstOverride.ParameterId = FirstParameter.Id;
				FirstOverride.FloatValue = 0.25f;
				SecondOverride.ParameterId = SecondParameter.Id;
				SecondOverride.FloatValue = -0.5f;
				C->Overrides = {FirstOverride, SecondOverride};
			}
			FVoxelLandProgramDraft Draft;
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandGraphCompiler::Compile(C->Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Program)
				|| !FVoxelLandProgramCodec::Pack(C->Source, *Program, C->Payload, Error))
			{
				return;
			}
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			C->CurveKeys = {First, Last};
			C->CookedCurve.SourceKeys = C->CurveKeys;
			C->CookedCurve.BakeVersion = 1;
			C->CookedCurve.SamplesQ8.Init(0, 4097);
			FCanonicalWriter W;
			for (int32 Value : C->CookedCurve.SamplesQ8)
			{
				W.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
			FMemory::Memcpy(&C->CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		FVoxelLandM4IdentityRecord Record;
		bBuilt = VoxelLandM4WorldIdentity::Capture(S, Record, Error);
		if (!bBuilt)
		{
			return;
		}
		bStable = VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		// In sorted order, the v1 and v2 payloads differ only in the two
		// little-endian encoding fields. This reconstructs the legacy v1
		// envelope independently of the compatibility capture function.
		FVoxelLandM4IdentityRecord Version2;
		const bool bVersion2Captured = VoxelLandM4WorldIdentity::CaptureVersion(S, 2, Version2, Error);
		FVoxelLandM4IdentityRecord LegacyFixture = Version2;
		if (bVersion2Captured && LegacyFixture.CanonicalBytes.Num() > 41 && LegacyFixture.CanonicalBytes[4] == 2
			&& LegacyFixture.CanonicalBytes[37] == 2)
		{
			LegacyFixture.FormatVersion = 1;
			LegacyFixture.CanonicalBytes[4] = 1;
			LegacyFixture.CanonicalBytes[37] = 1;
			const uint64 Hash =
				FXxHash64::HashBuffer(LegacyFixture.CanonicalBytes.GetData(), LegacyFixture.CanonicalBytes.Num()).Hash;
			FMemory::Memcpy(&LegacyFixture.FingerprintBits, &Hash, sizeof(Hash));
			FVoxelLandM4IdentityRecord ActualLegacy;
			bLegacyExact = VoxelLandM4WorldIdentity::CaptureVersion(S, 1, ActualLegacy, Error)
						&& ActualLegacy.CanonicalBytes == LegacyFixture.CanonicalBytes;
			bLegacyMatch = VoxelLandM4WorldIdentity::Matches(LegacyFixture, S, Error);
			Swap(S.Continental.Overrides[0], S.Continental.Overrides[1]);
			bCanonicalReordered = VoxelLandM4WorldIdentity::Matches(Record, S, Error);
			bLegacyOrderSensitive = !VoxelLandM4WorldIdentity::Matches(LegacyFixture, S, Error);
			Swap(S.Continental.Overrides[0], S.Continental.Overrides[1]);
		}
		// Repack a graph after adding an unreachable node. Its compiled program
		// and prepass dependencies remain the same, but source identity differs.
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> OriginalProgram;
		TArray<uint8> PrepassBefore, PrepassAfter;
		if (bVersion2Captured
			&& FVoxelLandProgramCodec::Load(S.Continental.Source, S.Continental.Payload, OriginalProgram, Error)
			&& !VoxelLandM4PrepassIdentity::BuildCandidate(
				S, *OriginalProgram, *OriginalProgram, *OriginalProgram, PrepassBefore))
		{
			const FVoxelLandSerializedProgram OriginalPayload = S.Continental.Payload;
			FVoxelLandGraphNodeRecord Unused = Constant;
			Unused.Id = FGuid(8, 1, 1, 1);
			Unused.Pins[0].Id = FGuid(8, 2, 1, 1);
			Unused.Pins[1].Id = FGuid(8, 2, 1, 2);
			S.Continental.Source.Nodes.Add(Unused);
			FVoxelLandSerializedProgram ChangedPayload;
			if (FVoxelLandProgramCodec::Pack(S.Continental.Source, *OriginalProgram, ChangedPayload, Error))
			{
				S.Continental.Payload = ChangedPayload;
				bSourceBound = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
				bV2Compatible = VoxelLandM4WorldIdentity::Matches(Version2, S, Error);
				bPrepassReused = !VoxelLandM4PrepassIdentity::BuildCandidate(
									 S, *OriginalProgram, *OriginalProgram, *OriginalProgram, PrepassAfter)
							  && PrepassBefore == PrepassAfter;
			}
			S.Continental.Source.Nodes.Pop();
			S.Continental.Payload = OriginalPayload;
		}
		FVoxelLandParameterOverride Unknown;
		Unknown.ParameterId = FGuid(9, 9, 9, 9);
		S.Continental.Overrides.Add(Unknown);
		FVoxelLandM4IdentityRecord Invalid;
		bInvalidOverride = !VoxelLandM4WorldIdentity::Capture(S, Invalid, Error)
						&& !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.Continental.Overrides.Pop();
		++S.Peaks.CookedCurve.SamplesQ8[100];
		FCanonicalWriter TamperedCurve;
		for (int32 Value : S.Peaks.CookedCurve.SamplesQ8)
		{
			TamperedCurve.U32(uint32(Value));
		}
		uint64 TamperedHash = FXxHash64::HashBuffer(TamperedCurve.Bytes.GetData(), TamperedCurve.Bytes.Num()).Hash;
		FMemory::Memcpy(&S.Peaks.CookedCurve.SamplesHash, &TamperedHash, sizeof(TamperedHash));
		bInvalidBake = !VoxelLandM4WorldIdentity::Capture(S, Invalid, Error)
					&& !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		--S.Peaks.CookedCurve.SamplesQ8[100];
		FCanonicalWriter OriginalCurve;
		for (int32 Value : S.Peaks.CookedCurve.SamplesQ8)
		{
			OriginalCurve.U32(uint32(Value));
		}
		TamperedHash = FXxHash64::HashBuffer(OriginalCurve.Bytes.GetData(), OriginalCurve.Bytes.Num()).Hash;
		FMemory::Memcpy(&S.Peaks.CookedCurve.SamplesHash, &TamperedHash, sizeof(TamperedHash));
		TArray<uint8> Stored;
		FMemoryWriter Writer(Stored);
		FVoxelLandM4IdentityRecord::StaticStruct()->SerializeItem(Writer, &Record, nullptr);
		FMemoryReader Reader(Stored);
		FVoxelLandM4IdentityRecord Restored;
		FVoxelLandM4IdentityRecord::StaticStruct()->SerializeItem(Reader, &Restored, nullptr);
		bRoundTrip = !Reader.IsError() && VoxelLandM4WorldIdentity::Matches(Restored, S, Error);
		++S.Seed;
		bSeed = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		--S.Seed;
		S.bNormalizeSlope = true;
		bShape = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.bNormalizeSlope = false;
		S.HeightDetail.bEnabled = true;
		S.HeightDetail.AmplitudeQ8 = 256;
		bDetail = !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.HeightDetail.bEnabled = false;
		S.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/Test/Material.Material"));
		bMaterial = VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		FVoxelLandM4IdentityRecord Unlimited;
		bUnlimited = VoxelLandM4WorldIdentity::Capture(S, Unlimited, Error)
				  && VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error)
				  && !VoxelLandM4WorldIdentity::Matches(Record, S, Error);
		S.UnlimitedBackend = EVoxelLandM4UnlimitedBackend::FixedLattice;
		FVoxelLandM4IdentityRecord Fixed, WrongVersion;
		bFixedIdentity = VoxelLandM4WorldIdentity::Capture(S, Fixed, Error) && Fixed.FormatVersion == 5
					  && VoxelLandM4WorldIdentity::Matches(Fixed, S, Error)
					  && !VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error)
					  && !VoxelLandM4WorldIdentity::CaptureVersion(S, 4, WrongVersion, Error);
		S.UnlimitedBackend = EVoxelLandM4UnlimitedBackend::LegacyFloat;
		const int32 SupportedVersion = Unlimited.FormatVersion;
		Unlimited.FormatVersion = 999;
		bVersion = !VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error);
		Unlimited.FormatVersion = SupportedVersion;
		Unlimited.CanonicalBytes[0] ^= 1;
		bCorrupt = !VoxelLandM4WorldIdentity::Matches(Unlimited, S, Error);
	}).Get();
	TestTrue(FString::Printf(TEXT("identity captures: %s"), *Error), bBuilt);
	TestTrue(TEXT("identical inputs match"), bStable);
	TestTrue(TEXT("v1 bytes preserve legacy encoding"), bLegacyExact);
	TestTrue(TEXT("legacy identity still matches"), bLegacyMatch);
	TestTrue(TEXT("v2 identity ignores override order"), bCanonicalReordered);
	TestTrue(TEXT("v1 identity retains original override order"), bLegacyOrderSensitive);
	TestTrue(TEXT("v3 full identity binds validated source changes"), bSourceBound);
	TestTrue(TEXT("v2 identity retains legacy source-independent semantics"), bV2Compatible);
	TestTrue(TEXT("unreachable source edit retains the upstream prepass key"), bPrepassReused);
	TestTrue(TEXT("capture and matching reject unknown overrides"), bInvalidOverride);
	TestTrue(TEXT("capture and matching reject a mismatched curve despite its updated hash"), bInvalidBake);
	TestTrue(TEXT("serialized identity matches after round trip"), bRoundTrip);
	TestTrue(TEXT("seed changes reject reuse"), bSeed);
	TestTrue(TEXT("slope changes reject reuse"), bShape);
	TestTrue(TEXT("height detail changes reject reuse"), bDetail);
	TestTrue(TEXT("material does not affect identity"), bMaterial);
	TestTrue(TEXT("fixed lattice has distinct versioned identity"), bFixedIdentity);
	TestTrue(TEXT("unlimited identity is distinct and matches itself"), bUnlimited);
	TestTrue(TEXT("unknown record versions fail"), bVersion);
	TestTrue(TEXT("corrupt bytes fail"), bCorrupt);
	return true;
}
#endif
