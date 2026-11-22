#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM4OverrideValidation.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4Domain.h"
#include "Generation/VoxelLandM4ErosionRandom.h"

namespace VoxelLandM4PrepassIdentity
{
namespace
{
FVoxelLandError M4IdentityFailure(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidParameter;
	Error.Message = Message;
	return Error;
}

void M4IdentityU64(FCanonicalWriter& W, uint64 Value)
{
	W.U32(uint32(Value));
	W.U32(uint32(Value >> 32));
}

void M4IdentityGuid(FCanonicalWriter& W, const FGuid& Id)
{
	W.U32(Id.A);
	W.U32(Id.B);
	W.U32(Id.C);
	W.U32(Id.D);
}

void M4IdentityFloat(FCanonicalWriter& W, float Value)
{
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	W.U32(Bits);
}

void M4IdentityProgram(FCanonicalWriter& W, const FVoxelLandProgramDraft& D)
{
	W.U32(D.FormatVersion);
	W.U32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	W.U32(uint32(D.RegisterCount));
	W.U32(uint32(D.Instructions.Num()));
	for (const FVoxelLandInstruction& I : D.Instructions)
	{
		W.U16(uint16(I.Op));
		W.U16(I.Dst);
		W.U16(I.A);
		W.U16(I.B);
		W.U16(I.C);
		W.U16(I.D);
		W.U16(I.E);
		W.U16(I.F);
		W.U32(I.Imm);
	}
	W.U32(uint32(D.Constants.Num()));
	for (float Value : D.Constants)
	{
		M4IdentityFloat(W, Value);
	}
	W.U32(uint32(D.Parameters.Num()));
	for (const FVoxelLandParameterSlot& P : D.Parameters)
	{
		W.String(P.Name);
		W.U8(uint8(P.Type));
		M4IdentityFloat(W, P.FloatDefault);
		W.U32(uint32(P.IntDefault));
	}
	W.U32(uint32(D.Seeds.Num()));
	for (const FVoxelLandSeedSlot& S : D.Seeds)
	{
		M4IdentityGuid(W, S.NodeId);
		W.U32(S.Salt);
	}
	W.U32(uint32(D.Noise.Num()));
	for (const FVoxelLandNoiseConfig& N : D.Noise)
	{
		W.U32(N.SeedSlot);
		W.Source(N.Frequency);
		W.Source(N.Octaves);
		W.Source(N.Gain);
		W.Source(N.Lacunarity);
	}
}

bool M4IdentityChannel(FCanonicalWriter& W, const FVoxelLandM4ControlSnapshot& Channel,
	const FVoxelLandProgram& Program, uint32 EncodingVersion)
{
	FString OverrideError;
	if (!VoxelLandM4OverrideValidation::Validate(Channel, OverrideError)
		|| VoxelLandM4ControlGrid::ValidateProgram(Program) || Channel.CookedCurve.BakeVersion != 1
		|| Channel.CookedCurve.SamplesQ8.Num() != 4097 || Channel.Payload.Parameters.Num() > 128
		|| Channel.Overrides.Num() > 128)
	{
		return false;
	}
	M4IdentityProgram(W, Program.GetData());
	// Parameter IDs map authored overrides to the VM names used by the generator.
	W.U32(uint32(Channel.Payload.Parameters.Num()));
	for (const FVoxelLandSerializedParameter& P : Channel.Payload.Parameters)
	{
		if (!P.Id.IsValid() || P.Name.Len() > 128 || !FMath::IsFinite(P.FloatDefault))
		{
			return false;
		}
		M4IdentityGuid(W, P.Id);
		W.String(P.Name);
		W.U8(P.Type);
		M4IdentityFloat(W, P.FloatDefault);
		W.U32(uint32(P.IntDefault));
	}
	W.U32(uint32(Channel.Overrides.Num()));
	TArray<const FVoxelLandParameterOverride*> Overrides;
	Overrides.Reserve(Channel.Overrides.Num());
	for (const FVoxelLandParameterOverride& O : Channel.Overrides)
	{
		Overrides.Add(&O);
	}
	if (EncodingVersion >= 2)
	{
		Overrides.Sort([](const FVoxelLandParameterOverride& A, const FVoxelLandParameterOverride& B) {
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
	}
	for (const FVoxelLandParameterOverride* Entry : Overrides)
	{
		const FVoxelLandParameterOverride& O = *Entry;
		if (!O.ParameterId.IsValid())
		{
			return false;
		}
		M4IdentityGuid(W, O.ParameterId);
		W.U8(uint8(O.Type));
		switch (O.Type)
		{
			case EVoxelLandGraphValueType::Float:
				if (!FMath::IsFinite(O.FloatValue))
				{
					return false;
				}
				M4IdentityFloat(W, O.FloatValue);
				break;
			case EVoxelLandGraphValueType::Int32:
				W.U32(uint32(O.IntValue));
				break;
			case EVoxelLandGraphValueType::Bool:
				W.U8(O.BoolValue ? 1 : 0);
				break;
			default:
				return false;
		}
	}
	W.U32(uint32(Channel.CookedCurve.BakeVersion));
	W.U32(uint32(Channel.CookedCurve.SamplesQ8.Num()));
	for (int32 Sample : Channel.CookedCurve.SamplesQ8)
	{
		if (Sample < -512000000 || Sample > 512000000)
		{
			return false;
		}
		W.U32(uint32(Sample));
	}
	return true;
}
}

FVoxelLandError BuildCandidate(const FVoxelLandM4WorldSnapshot& Snapshot, const FVoxelLandProgram& Continental,
	const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks, TArray<uint8>& OutBytes, uint32 EncodingVersion)
{
	FString StageError;
	if (!VoxelLandM4HeightStageValidation::Validate(Snapshot, StageError))
	{
		return M4IdentityFailure(*StageError);
	}
	if ((EncodingVersion != 1 && EncodingVersion != 2 && EncodingVersion != 3)
		|| (Snapshot.HeightStage.Version == 1 && EncodingVersion != 3)
		|| (Snapshot.HeightStage.Version == 0 && EncodingVersion == 3)
		|| Snapshot.Domain.Mode != EVoxelLandM4DomainMode::Finite || VoxelLandM4Domain::Validate(Snapshot.Domain)
		|| Snapshot.FormatVersion != 1 || Snapshot.BaseHeightQ8 < -512000000 || Snapshot.BaseHeightQ8 > 512000000)
	{
		return M4IdentityFailure(TEXT("Invalid finite M4 prepass identity inputs"));
	}
	if (Snapshot.Domain.bGlobalErosion
		&& (!Snapshot.ErosionStageId.IsValid() || Snapshot.ErosionSalt < 0 || Snapshot.DropletCount < 0
			|| Snapshot.DropletCount > 65536 || Snapshot.Erosion.MaxSteps < 0 || Snapshot.Erosion.MaxSteps > 128
			|| Snapshot.Erosion.ThermalIterations < 0 || Snapshot.Erosion.ThermalIterations > 32
			|| Snapshot.Erosion.CapacityMaxQ8 < 0 || Snapshot.Erosion.CapacityMaxQ8 > 512000000
			|| Snapshot.Erosion.TalusQ8 < 0 || Snapshot.Erosion.TalusQ8 > 512000000
			|| Snapshot.Erosion.MaxTransferQ8 < 0 || Snapshot.Erosion.MaxTransferQ8 > 512000000))
	{
		return M4IdentityFailure(TEXT("Invalid M4 erosion identity inputs"));
	}
	FCanonicalWriter W;
	W.U8('V');
	W.U8('L');
	W.U8('P');
	W.U8('K');
	W.U32(EncodingVersion); // candidate encoding
	W.U32(1); // three-channel height-combination and CPU prepass algorithm
	if (EncodingVersion >= 3)
	{
		W.U32(uint32(Snapshot.HeightStage.Version));
	}
	M4IdentityU64(W, uint64(Snapshot.Seed));
	M4IdentityU64(W, uint64(Snapshot.Domain.OriginXCm));
	M4IdentityU64(W, uint64(Snapshot.Domain.OriginYCm));
	W.U32(uint32(Snapshot.Domain.CellsX));
	W.U32(uint32(Snapshot.Domain.CellsY));
	M4IdentityU64(W, uint64(Snapshot.Domain.GridSpacingCm));
	W.U32(uint32(Snapshot.BaseHeightQ8));
	W.U8(Snapshot.Domain.bGlobalErosion ? 1 : 0);
	if (Snapshot.Domain.bGlobalErosion)
	{
		W.U32(VoxelLandM4ErosionRandom::AlgorithmVersion);
		M4IdentityGuid(W, Snapshot.ErosionStageId);
		W.U32(uint32(Snapshot.ErosionSalt));
		W.U32(uint32(Snapshot.DropletCount));
		W.U32(uint32(Snapshot.Erosion.MaxSteps));
		W.U32(uint32(Snapshot.Erosion.ThermalIterations));
		M4IdentityU64(W, uint64(Snapshot.Erosion.CapacityMaxQ8));
		M4IdentityU64(W, uint64(Snapshot.Erosion.TalusQ8));
		M4IdentityU64(W, uint64(Snapshot.Erosion.MaxTransferQ8));
	}
	if (!M4IdentityChannel(W, Snapshot.Continental, Continental, EncodingVersion)
		|| !M4IdentityChannel(W, Snapshot.Mountain, Mountain, EncodingVersion)
		|| !M4IdentityChannel(W, Snapshot.Peaks, Peaks, EncodingVersion))
	{
		return M4IdentityFailure(TEXT("Unsupported or invalid M4 control identity inputs"));
	}
	OutBytes = MoveTemp(W.Bytes);
	return {};
}
}
