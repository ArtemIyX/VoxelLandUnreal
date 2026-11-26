#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "VoxelLandIdentitySerialization.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightStageValidation.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "World/Terrain/Persistence/VoxelLandTerrainPrepassIdentity.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include "World/Terrain/Curves/VoxelLandCurveValidation.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"
#include "World/Terrain/Worlds/VoxelLandAuthoredHeightProgram.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorldPreflight.h"

class FVoxelLandM4WorldIdentityInternals final
{
public:
	static void M4WorldIdentityU64(FCanonicalWriter& W, uint64 V);
	static void M4WorldIdentityGuid(FCanonicalWriter& W, const FGuid& G);
	static bool M4WorldIdentityOverrides(FCanonicalWriter& W, const FVoxelLandM4ControlSnapshot& Channel);
	static int64 M4WorldIdentityHash(const TArray<uint8>& Bytes);
	static void WriteShaping(FCanonicalWriter& W, const FVoxelLandM4WorldSnapshot& S);
	static bool CaptureAuthoredHeight(
		const FVoxelLandM4WorldSnapshot& S, FVoxelLandM4IdentityRecord& Out, FString& Error);
};

void FVoxelLandM4WorldIdentityInternals::M4WorldIdentityU64(FCanonicalWriter& W, uint64 V)
{
	W.U32(uint32(V));
	W.U32(uint32(V >> 32));
}

void FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(FCanonicalWriter& W, const FGuid& G)
{
	W.U32(G.A);
	W.U32(G.B);
	W.U32(G.C);
	W.U32(G.D);
}

bool FVoxelLandM4WorldIdentityInternals::M4WorldIdentityOverrides(
	FCanonicalWriter& W, const FVoxelLandM4ControlSnapshot& Channel)
{
	TArray<const FVoxelLandParameterOverride*> Sorted;
	for (const FVoxelLandParameterOverride& Override : Channel.Overrides)
	{
		Sorted.Add(&Override);
	}
	Sorted.Sort([](const FVoxelLandParameterOverride& A, const FVoxelLandParameterOverride& B) {
		const FGuid& X = A.ParameterId;
		const FGuid& Y = B.ParameterId;
		if (X.A != Y.A)
		{
			return X.A < Y.A;
		}
		if (X.B != Y.B)
		{
			return X.B < Y.B;
		}
		if (X.C != Y.C)
		{
			return X.C < Y.C;
		}
		return X.D < Y.D;
	});
	W.U32(uint32(Sorted.Num()));
	for (const FVoxelLandParameterOverride* O : Sorted)
	{
		M4WorldIdentityGuid(W, O->ParameterId);
		W.U8(uint8(O->Type));
		switch (O->Type)
		{
			case EVoxelLandGraphValueType::Float:
				if (!FMath::IsFinite(O->FloatValue))
				{
					return false;
				}
				W.Float(O->FloatValue);
				break;
			case EVoxelLandGraphValueType::Int32:
				W.U32(uint32(O->IntValue));
				break;
			case EVoxelLandGraphValueType::Bool:
				W.U8(O->BoolValue ? 1 : 0);
				break;
			default:
				return false;
		}
	}
	return true;
}

int64 FVoxelLandM4WorldIdentityInternals::M4WorldIdentityHash(const TArray<uint8>& Bytes)
{
	const uint64 Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
	int64 Bits = 0;
	FMemory::Memcpy(&Bits, &Hash, sizeof(Bits));
	return Bits;
}

void FVoxelLandM4WorldIdentityInternals::WriteShaping(FCanonicalWriter& W, const FVoxelLandM4WorldSnapshot& S)
{
	W.U8(S.bNormalizeSlope ? 1 : 0);

	W.U32(S.Smooth.Version);

	W.U8(S.Smooth.bEnabled ? 1 : 0);

	if (S.Smooth.bEnabled)

	{

		W.Float(S.Smooth.Strength);
	}

	W.U8(S.bOverhangEnabled ? 1 : 0);

	if (S.bOverhangEnabled)

	{

		W.U32(uint32(S.OverhangAmplitudeQ8));

		W.U32(uint32(S.OverhangBandQ8));

		W.Float(S.OverhangFrequencyPerCm);

		FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.OverhangNodeId);

		W.U32(uint32(S.OverhangSalt));
	}

	W.U32(uint32(S.HeightDetail.AlgorithmVersion));

	W.U8(S.HeightDetail.bEnabled ? 1 : 0);

	if (S.HeightDetail.bEnabled)

	{

		W.U32(uint32(S.HeightDetail.AmplitudeQ8));

		W.Float(S.HeightDetail.FrequencyPerCm);

		FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.HeightDetail.NodeId);

		W.U32(S.HeightDetail.Salt);
	}
}

bool FVoxelLandM4WorldIdentityInternals::CaptureAuthoredHeight(
	const FVoxelLandM4WorldSnapshot& S, FVoxelLandM4IdentityRecord& Out, FString& Error)
{
	if (!VoxelLandM4SavedWorldPreflight::ValidateSnapshot(S, Error))
	{
		return false;
	}
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Source;
	if (!VoxelLandAuthoredHeightProgram::CreateSource(S, Settings, Source, Error))
	{
		return false;
	}
	FCanonicalWriter W;
	W.U32(0x35574c56); // VLW5, little-endian.
	W.U32(VoxelLandM4WorldIdentity::AuthoredHeightVersion);
	W.U32(1); // authored Float to Q8 height conversion
	W.U32(1); // fixed-lattice numeric and shaping contract
	W.U8(uint8(S.Domain.Mode));
	W.U32(uint32(S.VoxelSizeCm));
	M4WorldIdentityU64(W, uint64(S.Seed));
	M4WorldIdentityGuid(W, S.AuthoredHeightOutputId);
	M4WorldIdentityU64(W, Source->GetProgram().GetFingerprint());
	W.U32(uint32(S.HeightScaleCm));
	W.U32(uint32(S.BaseHeightQ8));
	if (S.Domain.Mode == EVoxelLandM4DomainMode::Finite)
	{
		const TArray<uint8> Prepass = VoxelLandAuthoredHeightProgram::BuildPrepassKey(S, Source->GetProgram());
		W.U32(uint32(Prepass.Num()));
		W.Bytes.Append(Prepass);
	}
	else
	{
		W.U32(0);
	}
	WriteShaping(W, S);
	FVoxelLandM4IdentityRecord Result;
	Result.FormatVersion = VoxelLandM4WorldIdentity::AuthoredHeightVersion;
	Result.CanonicalBytes = MoveTemp(W.Bytes);
	Result.FingerprintBits = M4WorldIdentityHash(Result.CanonicalBytes);
	Out = MoveTemp(Result);
	return true;
}

bool VoxelLandM4WorldIdentity::CaptureVersion(
	const FVoxelLandM4WorldSnapshot& S, int32 Version, FVoxelLandM4IdentityRecord& Out, FString& Error)
{
	Error.Reset();
	const bool bAuthored = S.AuthoredHeightOutputId.IsValid();
	if ((bAuthored && Version != AuthoredHeightVersion) || (!bAuthored && Version == AuthoredHeightVersion))
	{
		Error = TEXT(
			"Height identity requires a named Float output. Reauthor legacy three-channel terrain and regenerate the world.");
		return false;
	}
	if ((Version != 1 && Version != AuthoredHeightVersion)
		|| (S.Domain.Mode == EVoxelLandM4DomainMode::Unlimited
			&& (S.Domain.bGlobalErosion
				|| (S.bOverhangEnabled && S.OverhangAmplitudeQ8 != 0
					&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(S.OverhangFrequencyPerCm))
				|| (S.HeightDetail.bEnabled && S.HeightDetail.AmplitudeQ8 != 0
					&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(S.HeightDetail.FrequencyPerCm))))
		|| IsInGameThread() || S.FormatVersion != 1 || S.VoxelSizeCm < 1 || S.VoxelSizeCm > 2000000
		|| (S.Domain.Mode == EVoxelLandM4DomainMode::Unlimited && S.VoxelSizeCm > 134217)
		|| VoxelLandM4Domain::Validate(S.Domain) || S.Smooth.Version != 1 || !FMath::IsFinite(S.Smooth.Strength)
		|| S.Smooth.Strength < 0.f || S.Smooth.Strength > 1.f || S.OverhangAmplitudeQ8 < 0
		|| S.OverhangAmplitudeQ8 > 128000 || S.OverhangBandQ8 < 1 || S.OverhangBandQ8 > 1280000
		|| S.OverhangAmplitudeQ8 > S.OverhangBandQ8 || !FMath::IsFinite(S.OverhangFrequencyPerCm)
		|| S.OverhangFrequencyPerCm <= 0.f || S.OverhangFrequencyPerCm > 1.f || S.OverhangSalt < 0
		|| !S.OverhangNodeId.IsValid() || S.HeightDetail.AlgorithmVersion != 1 || S.HeightDetail.AmplitudeQ8 < 0
		|| S.HeightDetail.AmplitudeQ8 > 1280000 || !FMath::IsFinite(S.HeightDetail.FrequencyPerCm)
		|| S.HeightDetail.FrequencyPerCm <= 0.f || S.HeightDetail.FrequencyPerCm > 1.f
		|| !S.HeightDetail.NodeId.IsValid())
	{
		Error = TEXT("Invalid or unsupported M4 identity inputs, or capture was called on the game thread");
		return false;
	}
	if (bAuthored)
	{
		return FVoxelLandM4WorldIdentityInternals::CaptureAuthoredHeight(S, Out, Error);
	}
	if (!VoxelLandM4HeightStageValidation::Validate(S, Error))
	{
		return false;
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	for (int32 I = 0; I < 3; ++I)
	{
		if (!VoxelLandM4OverrideValidation::Validate(*Channels[I], Error))
		{
			return false;
		}
		if (!VoxelLandM4CurveValidation::Validate(*Channels[I], Error))
		{
			return false;
		}
		if (!FVoxelLandProgramCodec::Load(Channels[I]->Source, Channels[I]->Payload, Programs[I], Error))
		{
			return false;
		}
		if (S.Domain.Mode == EVoxelLandM4DomainMode::Unlimited)
		{
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Programs[I];
			VoxelLandM4OverrideValidation::BuildProfile(*Channels[I], S.VoxelSizeCm, Settings.Profile);
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
			{
				Error = Failure.Message;
				return false;
			}
			if (const FVoxelLandError Failure = VoxelLandM4FixedLattice::Validate(*Generator))
			{
				Error = Failure.Message;
				return false;
			}
		}
	}
	return VoxelLandM4WorldIdentityInternal::SerializeValidated(
		S, Version, *Programs[0], *Programs[1], *Programs[2], Out, Error);
}

bool VoxelLandM4WorldIdentityInternal::SerializeValidated(const FVoxelLandM4WorldSnapshot& S, int32 Version,
	const FVoxelLandProgram& Continental, const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks,
	FVoxelLandM4IdentityRecord& Out, FString& Error)
{
	TArray<uint8> Candidate;
	if (const FVoxelLandError Failure =
			VoxelLandM4PrepassIdentity::BuildWorldInputs(S, Continental, Mountain, Peaks, Candidate))
	{
		Error = Failure.Message;
		return false;
	}
	return SerializeFromPrepass(S, Version, Continental, Mountain, Peaks, Candidate, Out, Error);
}

bool VoxelLandM4WorldIdentityInternal::SerializeFromPrepass(const FVoxelLandM4WorldSnapshot& S, int32 Version,
	const FVoxelLandProgram& Continental, const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks,
	const TArray<uint8>& Candidate, FVoxelLandM4IdentityRecord& Out, FString& Error)
{
	if (Version != 1)
	{
		Error = TEXT("Unsupported world identity version");
		return false;
	}

	FCanonicalWriter W;
	W.U8('V');
	W.U8('L');
	W.U8('W');
	W.U8('5');
	W.U32(uint32(Version)); // identity encoding
	W.U32(1);               // channel seed namespaces and height combination
	W.U32(1);               // control quantization, curve lookup and bilinear interpolation
	W.U32(1);               // height-to-density / slope normalization
	W.U32(1);               // overhang algorithm
	W.U8(uint8(S.Domain.Mode));
	W.U32(uint32(S.VoxelSizeCm));
	W.U32(uint32(Candidate.Num()));
	W.Bytes.Append(Candidate);
	const FVoxelLandProgram* Programs[] = {&Continental, &Mountain, &Peaks};
	for (const auto* Program : Programs)
	{
		FVoxelLandM4WorldIdentityInternals::M4WorldIdentityU64(W, Program->GetFingerprint());
	}
	FVoxelLandM4WorldIdentityInternals::WriteShaping(W, S);
	const FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
	for (const FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		FVoxelLandM4WorldIdentityInternals::M4WorldIdentityU64(W, uint64(Channel->Payload.SourceFingerprint));
		if (!FVoxelLandM4WorldIdentityInternals::M4WorldIdentityOverrides(W, *Channel))
		{
			Error = TEXT("Invalid world identity override");
			return false;
		}
	}
	W.U32(uint32(S.HeightStage.Version));
	FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.HeightStage.OutputNodeId);
	FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.HeightStage.ContinentalInputId);
	FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.HeightStage.MountainInputId);
	FVoxelLandM4WorldIdentityInternals::M4WorldIdentityGuid(W, S.HeightStage.PeaksInputId);
	W.U32(1); // fixed-lattice numeric/noise contract
	W.U32(1); // fixed-lattice 2D/3D shaping contract

	if (W.Bytes.Num() > 8 * 1024 * 1024)
	{
		Error = TEXT("M4 world identity exceeds its supported size");
		return false;
	}
	FVoxelLandM4IdentityRecord Result;
	Result.FormatVersion = Version;
	Result.CanonicalBytes = MoveTemp(W.Bytes);
	Result.FingerprintBits = FVoxelLandM4WorldIdentityInternals::M4WorldIdentityHash(Result.CanonicalBytes);
	Out = MoveTemp(Result);
	return true;
}

bool VoxelLandM4WorldIdentity::Capture(
	const FVoxelLandM4WorldSnapshot& S, FVoxelLandM4IdentityRecord& Out, FString& Error)
{
	return CaptureVersion(S, S.AuthoredHeightOutputId.IsValid() ? AuthoredHeightVersion : 1, Out, Error);
}

bool VoxelLandM4WorldIdentity::Matches(
	const FVoxelLandM4IdentityRecord& Record, const FVoxelLandM4WorldSnapshot& Snapshot, FString& Error)
{
	if ((Record.FormatVersion != 1 && Record.FormatVersion != AuthoredHeightVersion) || Record.CanonicalBytes.Num() < 8
		|| Record.CanonicalBytes.Num() > 8 * 1024 * 1024
		|| Record.FingerprintBits != FVoxelLandM4WorldIdentityInternals::M4WorldIdentityHash(Record.CanonicalBytes))
	{
		Error = TEXT("Unsupported or corrupt M4 identity record");
		return false;
	}
	FVoxelLandM4IdentityRecord Current;
	if (!CaptureVersion(Snapshot, Record.FormatVersion, Current, Error))
	{
		return false;
	}
	if (Record.CanonicalBytes != Current.CanonicalBytes)
	{
		Error = TEXT("M4 world identity does not match the current settings and programs");
		return false;
	}
	return true;
}

bool VoxelLandM4WorldIdentity::Regenerate(const FVoxelLandM4IdentityRecord& Record,
	const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& Error)
{
	if (IsCancelled() || !Matches(Record, Snapshot, Error) || IsCancelled())
	{
		return false;
	}
	return VoxelLandM4Preparation::Prepare(Snapshot, Lease, ValidationLease, IsCancelled, OutGenerator, Error);
}
