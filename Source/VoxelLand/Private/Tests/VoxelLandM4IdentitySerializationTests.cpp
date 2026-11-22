#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandM4DomainMode.h"
#include "Data/Enums/VoxelLandM4UnlimitedBackend.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4IdentitySerialization.h"
#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM4WorldIdentity.h"

namespace
{
// Independently encodes the current world envelope, without FCanonicalWriter
// or the production world serializer. This fixture has no overrides.
TArray<uint8> M4SerializationReference(const FVoxelLandM4WorldSnapshot& S, int32 Version, const TArray<uint8>& Prepass,
	const TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> (&Programs)[3])
{
	TArray<uint8> Bytes;
	auto U8 = [&Bytes](uint8 V) {
		Bytes.Add(V);
	};
	auto U32 = [&U8](uint32 V) {
		for (uint32 Shift = 0; Shift != 32; Shift += 8)
		{
			U8(uint8(V >> Shift));
		}
	};
	auto U64 = [&U8](uint64 V) {
		for (uint32 Shift = 0; Shift != 64; Shift += 8)
		{
			U8(uint8(V >> Shift));
		}
	};
	auto Guid = [&U32](const FGuid& G) {
		U32(G.A);
		U32(G.B);
		U32(G.C);
		U32(G.D);
	};
	auto Float = [&U32](float V) {
		uint32 Bits = 0;
		if (V != 0.f)
		{
			FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		}
		U32(Bits);
	};
	U8('V');
	U8('L');
	U8('W');
	U8('5');
	U32(uint32(Version));
	U32(1);
	U32(1);
	U32(1);
	U32(1);
	U8(uint8(S.Domain.Mode));
	U32(uint32(S.VoxelSizeCm));
	U32(uint32(Prepass.Num()));
	for (uint8 Byte : Prepass)
	{
		U8(Byte);
	}
	for (const auto& Program : Programs)
	{
		U64(Program->GetFingerprint());
	}
	U8(S.bNormalizeSlope ? 1 : 0);
	U32(S.Smooth.Version);
	U8(S.Smooth.bEnabled ? 1 : 0);
	if (S.Smooth.bEnabled)
	{
		Float(S.Smooth.Strength);
	}
	U8(S.bOverhangEnabled ? 1 : 0);
	if (S.bOverhangEnabled)
	{
		U32(uint32(S.OverhangAmplitudeQ8));
		U32(uint32(S.OverhangBandQ8));
		Float(S.OverhangFrequencyPerCm);
		Guid(S.OverhangNodeId);
		U32(uint32(S.OverhangSalt));
	}
	U32(uint32(S.HeightDetail.AlgorithmVersion));
	U8(S.HeightDetail.bEnabled ? 1 : 0);
	if (S.HeightDetail.bEnabled)
	{
		U32(uint32(S.HeightDetail.AmplitudeQ8));
		Float(S.HeightDetail.FrequencyPerCm);
		Guid(S.HeightDetail.NodeId);
		U32(S.HeightDetail.Salt);
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
	for (const FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		U64(uint64(Channel->Payload.SourceFingerprint));
		// This fixture has no authored overrides in any channel.
		U32(0);
	}
	U32(uint32(S.HeightStage.Version));
	Guid(S.HeightStage.OutputNodeId);
	Guid(S.HeightStage.ContinentalInputId);
	Guid(S.HeightStage.MountainInputId);
	Guid(S.HeightStage.PeaksInputId);
	U8(uint8(S.UnlimitedBackend));
	U32(1);
	U32(1);

	return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4IdentitySerializationTest, "VoxelLand.M4.WorldIdentity.SharedEncoder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM4IdentitySerializationTest::RunTest(const FString& Parameters)
{
	bool Matches[2] = {};
	bool ReferenceMatches[2] = {};
	bool HeightStageMatches[2] = {};
	bool bFailureAtomic = false;
	bool bCurrentMagic[2] = {};
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
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto& Channel = *Channels[Index];
			Channel.Source.Nodes = {Constant, Output};
			Channel.Source.Links = {Link};
			FVoxelLandProgramDraft Draft;
			if (!FVoxelLandGraphCompiler::Compile(Channel.Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Programs[Index])
				|| !FVoxelLandProgramCodec::Pack(Channel.Source, *Programs[Index], Channel.Payload, Error))
			{
				return;
			}
			FVoxelLandM4CurveAuthoringKey First, Last;
			Last.InputQ16 = 65535;
			Channel.CurveKeys = {First, Last};
			Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
			Channel.CookedCurve.BakeVersion = 1;
			Channel.CookedCurve.SamplesQ8.Init(0, 4097);
			FCanonicalWriter CurveWriter;
			for (int32 Value : Channel.CookedCurve.SamplesQ8)
			{
				CurveWriter.U32(uint32(Value));
			}
			const uint64 Hash = FXxHash64::HashBuffer(CurveWriter.Bytes.GetData(), CurveWriter.Bytes.Num()).Hash;
			FMemory::Memcpy(&Channel.CookedCurve.SamplesHash, &Hash, sizeof(Hash));
		}
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			S.Domain.Mode = Mode == 0 ? EVoxelLandM4DomainMode::Finite : EVoxelLandM4DomainMode::Unlimited;
			S.UnlimitedBackend =
				Mode == 0 ? EVoxelLandM4UnlimitedBackend::LegacyFloat : EVoxelLandM4UnlimitedBackend::FixedLattice;
			S.bOverhangEnabled = Mode != 0;
			S.OverhangAmplitudeQ8 = Mode != 0 ? 256 : 0;
			FVoxelLandM4IdentityRecord Captured;
			if (!VoxelLandM4WorldIdentity::Capture(S, Captured, Error))
			{
				return;
			}
			TArray<uint8> Candidate;
			if (VoxelLandM4PrepassIdentity::BuildWorldInputs(S, *Programs[0], *Programs[1], *Programs[2], Candidate))
			{
				return;
			}
			FVoxelLandM4IdentityRecord Direct;
			if (!VoxelLandM4WorldIdentityInternal::SerializeFromPrepass(
					S, 1, *Programs[0], *Programs[1], *Programs[2], Candidate, Direct, Error))
			{
				return;
			}
			const uint8 CurrentPrefix[] = {'V', 'L', 'W', '5', 1, 0, 0, 0, 1, 0, 0, 0};
			bCurrentMagic[Mode] =
				Captured.CanonicalBytes.Num() >= UE_ARRAY_COUNT(CurrentPrefix)
				&& FMemory::Memcmp(Captured.CanonicalBytes.GetData(), CurrentPrefix, sizeof(CurrentPrefix)) == 0;
			Matches[Mode] = Captured.FormatVersion == 1 && Captured.CanonicalBytes == Direct.CanonicalBytes
						 && Captured.FingerprintBits == Direct.FingerprintBits;
			const TArray<uint8> Reference = M4SerializationReference(S, 1, Candidate, Programs);
			const uint64 ExpectedHash = FXxHash64::HashBuffer(Reference.GetData(), Reference.Num()).Hash;
			int64 ExpectedBits = 0;
			FMemory::Memcpy(&ExpectedBits, &ExpectedHash, sizeof(ExpectedBits));
			ReferenceMatches[Mode] = Captured.CanonicalBytes == Reference && Captured.FingerprintBits == ExpectedBits;
		}

		FVoxelLandM4IdentityRecord Retained;
		Retained.FormatVersion = 37;
		Retained.CanonicalBytes = {3, 9, 5};
		Retained.FingerprintBits = 123;
		bFailureAtomic = !VoxelLandM4WorldIdentity::CaptureVersion(S, 99, Retained, Error)
					  && Retained.FormatVersion == 37 && Retained.CanonicalBytes == TArray<uint8>({3, 9, 5})
					  && Retained.FingerprintBits == 123;

		// Three distinct projection pins belong to one authored height output.
		S.HeightStage.Version = 1;
		S.HeightStage.OutputNodeId = Output.Id;
		S.HeightStage.ContinentalInputId = FGuid(10, 0, 0, 1);
		S.HeightStage.MountainInputId = FGuid(10, 0, 0, 2);
		S.HeightStage.PeaksInputId = FGuid(10, 0, 0, 3);
		const FGuid Pins[] = {
			S.HeightStage.ContinentalInputId, S.HeightStage.MountainInputId, S.HeightStage.PeaksInputId};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto& Channel = *Channels[Index];
			Channel.Source.Nodes[1].Pins[0].Id = Pins[Index];
			Channel.Source.Links[0].TargetPin = Pins[Index];
			FVoxelLandProgramDraft Draft;
			if (!FVoxelLandGraphCompiler::Compile(Channel.Source, Draft, Error)
				|| FVoxelLandProgram::Validate(Draft, Programs[Index])
				|| !FVoxelLandProgramCodec::Pack(Channel.Source, *Programs[Index], Channel.Payload, Error))
			{
				return;
			}
		}
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			S.Domain.Mode = Mode == 0 ? EVoxelLandM4DomainMode::Finite : EVoxelLandM4DomainMode::Unlimited;
			S.UnlimitedBackend =
				Mode == 0 ? EVoxelLandM4UnlimitedBackend::LegacyFloat : EVoxelLandM4UnlimitedBackend::FixedLattice;
			S.bOverhangEnabled = Mode != 0;
			S.OverhangAmplitudeQ8 = Mode != 0 ? 256 : 0;
			FVoxelLandM4IdentityRecord Captured;
			if (!VoxelLandM4WorldIdentity::Capture(S, Captured, Error))
			{
				return;
			}
			TArray<uint8> Candidate;
			if (VoxelLandM4PrepassIdentity::BuildWorldInputs(S, *Programs[0], *Programs[1], *Programs[2], Candidate)
				|| Candidate.Num() < 17 || Candidate[4] != 1 || Candidate[12] != 1)
			{
				return;
			}
			const TArray<uint8> Reference = M4SerializationReference(S, 1, Candidate, Programs);
			const uint64 Hash = FXxHash64::HashBuffer(Reference.GetData(), Reference.Num()).Hash;
			int64 HashBits = 0;
			FMemory::Memcpy(&HashBits, &Hash, sizeof(HashBits));
			HeightStageMatches[Mode] = Captured.CanonicalBytes == Reference && Captured.FingerprintBits == HashBits;
		}
	}).Wait();
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		TestTrue(*FString::Printf(TEXT("mode %d has the fixed current header"), Mode), bCurrentMagic[Mode]);
		TestTrue(*FString::Printf(TEXT("mode %d shared encoder matches capture: %s"), Mode, *Error), Matches[Mode]);
		TestTrue(
			*FString::Printf(TEXT("mode %d matches independent envelope: %s"), Mode, *Error), ReferenceMatches[Mode]);
		TestTrue(*FString::Printf(TEXT("mode %d height-stage identity: %s"), Mode, *Error), HeightStageMatches[Mode]);
	}

	TestTrue(TEXT("invalid version leaves previous identity unchanged"), bFailureAtomic);
	return true;
}
#endif
