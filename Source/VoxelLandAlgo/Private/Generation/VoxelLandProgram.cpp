#include "Generation/VoxelLandGenerator.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Hash/xxhash.h"
#include "Operators/VoxelLandOperator.h"

namespace
{
FVoxelLandError MakeVoxelLandError(
	EVoxelLandErrorCode Code, const TCHAR* Message, int32 Instruction = INDEX_NONE, const FString& Parameter = {})
{
	FVoxelLandError Result;
	Result.Code = Code;
	Result.Message = Message;
	Result.Instruction = Instruction;
	Result.Parameter = Parameter;
	return Result;
}

bool NormalizeName(const FString& In, FString& Out)
{
	if (In.IsEmpty())
	{
		return false;
	}
	Out.Reset();
	for (TCHAR C : In)
	{
		if (C >= 'A' && C <= 'Z')
		{
			C += 'a' - 'A';
		}
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
		{
			return false;
		}
		Out.AppendChar(C);
	}
	return true;
}

uint64 ComputeProgramFingerprint(const FVoxelLandProgramDraft& D)
{
	FCanonicalWriter W;
	W.U32(D.FormatVersion);
    if (D.FormatVersion >= 2)
    {
        W.U32(FVoxelLandProgramDraft::CompilerSemanticVersion);
    }
	W.U32(uint32(D.RegisterCount));
	W.U32(D.Instructions.Num());
	for (const FVoxelLandInstruction& I : D.Instructions)
	{
		W.U16(uint16(I.Op));
		W.U16(I.Dst);
		W.U16(I.A);
		W.U16(I.B);
		W.U16(I.C);
		if (D.FormatVersion >= 2)
		{
			W.U16(I.D); W.U16(I.E); W.U16(I.F);
		}
		W.U32(I.Imm);
	}
	W.U32(D.Constants.Num());
	for (float V : D.Constants)
	{
		W.Float(V);
	}
	W.U32(D.Parameters.Num());
	for (const FVoxelLandParameterSlot& P : D.Parameters)
	{
		W.String(P.Name);
		W.U8(uint8(P.Type));
		W.Float(P.FloatDefault);
		W.U32(uint32(P.IntDefault));
	}
	W.U32(D.Seeds.Num());
	for (const FVoxelLandSeedSlot& S : D.Seeds)
	{
		W.U32(S.NodeId.A);
		W.U32(S.NodeId.B);
		W.U32(S.NodeId.C);
		W.U32(S.NodeId.D);
		W.U32(S.Salt);
	}
	W.U32(D.Noise.Num());
	for (const FVoxelLandNoiseConfig& N : D.Noise)
	{
		W.U32(N.SeedSlot);
		W.Source(N.Frequency);
		W.Source(N.Octaves);
		W.Source(N.Gain);
		W.Source(N.Lacunarity);
	}
	if (D.FormatVersion >= 2)
	{
        W.U32(uint32(D.ExternalFilterIndex));
		W.U8(D.SmoothFilter.bPresent ? 1 : 0);
		if (D.SmoothFilter.bPresent)
		{
			W.Source(D.SmoothFilter.Enabled);
			W.Source(D.SmoothFilter.Strength);
		}
		W.U32(D.ExternalOperators.Num());
		for (const FVoxelLandExternalOperator& Operator : D.ExternalOperators)
		{
			W.U32(Operator.Id.A); W.U32(Operator.Id.B); W.U32(Operator.Id.C); W.U32(Operator.Id.D);
			W.U32(uint32(Operator.Version));
            W.U8(uint8(Operator.Stage));
			W.U32(Operator.Inputs.Num());
			for (EVoxelLandGraphValueType Type : Operator.Inputs)
			{
				W.U8(uint8(Type));
			}
			W.U8(uint8(Operator.Output)); W.U32(uint32(Operator.SeedSlot));
			W.U32(Operator.Configuration.Num());
			for (uint8 Byte : Operator.Configuration)
			{
				W.U8(Byte);
			}
		}
	}
	return FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
}

bool SourceValid(const FVoxelLandProgramDraft& D, const FVoxelLandValueSource& Source, EVoxelLandParameterType Type)
{
	if (Source.bParameter)
	{
		return Source.Index < uint32(D.Parameters.Num()) && D.Parameters[Source.Index].Type == Type;
	}
	if (Source.Index >= uint32(D.Constants.Num()))
	{
		return false;
	}
	const float V = D.Constants[Source.Index];
	if (Type == EVoxelLandParameterType::Float)
	{
		return true;
	}
	if (Type == EVoxelLandParameterType::Bool)
	{
		return V == 0.f || V == 1.f;
	}
	return V >= 1.f && V <= 16.f && FMath::FloorToFloat(V) == V;
}
bool DraftWithinLimits(const FVoxelLandProgramDraft& D)
{
	if (D.Instructions.Num() > 8192 || D.Constants.Num() > 65536 || D.Parameters.Num() > 4096 ||
		D.Seeds.Num() > 4096 || D.Noise.Num() > 4096 || D.ExternalOperators.Num() > 4096)
	{
		return false;
	}
	int64 TotalConfiguration = 0;
	for (const FVoxelLandParameterSlot& Parameter : D.Parameters)
	{
		if (Parameter.Name.Len() > 256)
		{
			return false;
		}
	}
	for (const FVoxelLandExternalOperator& Operator : D.ExternalOperators)
	{
		TotalConfiguration += Operator.Configuration.Num();
		if (Operator.Configuration.Num() > 65536 || TotalConfiguration > 1048576 || Operator.Inputs.Num() > 3)
		{
			return false;
		}
	}
	return true;
}

FVoxelLandError ValidateProgramInputs(FVoxelLandProgramDraft& D)
{
	for (float V : D.Constants)
	{
		if (!FMath::IsFinite(V))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Non-finite constant"));
		}
	}
	TSet<FString> Names;
	for (FVoxelLandParameterSlot& P : D.Parameters)
	{
		FString Name;
		if (!NormalizeName(P.Name, Name) || Names.Contains(Name)
			|| (P.Type != EVoxelLandParameterType::Float && P.Type != EVoxelLandParameterType::Int32
				&& !(D.FormatVersion == 2 && P.Type == EVoxelLandParameterType::Bool))
			|| (P.Type == EVoxelLandParameterType::Bool && P.IntDefault != 0 && P.IntDefault != 1)
			|| !FMath::IsFinite(P.FloatDefault))
		{
			return MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate parameter"), INDEX_NONE, P.Name);
		}
		P.Name = Name;
		Names.Add(Name);
	}
	if (D.SmoothFilter.bPresent)
	{
		const FVoxelLandValueSource& Enabled = D.SmoothFilter.Enabled;
		const FVoxelLandValueSource& Strength = D.SmoothFilter.Strength;
		if (D.FormatVersion < 2 || !SourceValid(D, Enabled, EVoxelLandParameterType::Bool) ||
			!SourceValid(D, Strength, EVoxelLandParameterType::Float))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid smooth filter source"));
		}
		const float DefaultStrength = Strength.bParameter ? D.Parameters[Strength.Index].FloatDefault : D.Constants[Strength.Index];
		if (DefaultStrength < 0.f || DefaultStrength > 1.f)
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Smooth strength must be in [0,1]"));
		}
	}
	for (int32 SeedIndex = 0; SeedIndex < D.Seeds.Num(); ++SeedIndex)
	{
		const FVoxelLandSeedSlot& Seed = D.Seeds[SeedIndex];
		if (!Seed.NodeId.IsValid())
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid seed GUID"));
		}
		for (int32 PreviousSeedIndex = 0; PreviousSeedIndex < SeedIndex; ++PreviousSeedIndex)
		{
			if (D.Seeds[PreviousSeedIndex].NodeId == Seed.NodeId && D.Seeds[PreviousSeedIndex].Salt == Seed.Salt)
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Duplicate seed GUID and salt"));
			}
		}
	}
	for (const FVoxelLandNoiseConfig& N : D.Noise)
	{
		if (N.SeedSlot >= uint32(D.Seeds.Num()) || !SourceValid(D, N.Frequency, EVoxelLandParameterType::Float)
			|| !SourceValid(D, N.Octaves, EVoxelLandParameterType::Int32)
			|| !SourceValid(D, N.Gain, EVoxelLandParameterType::Float)
			|| !SourceValid(D, N.Lacunarity, EVoxelLandParameterType::Float))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid noise configuration source"));
		}
		const TFunction<float(const FVoxelLandValueSource&)> FloatDefault = [&](const FVoxelLandValueSource& S) {
			return S.bParameter ? D.Parameters[S.Index].FloatDefault : D.Constants[S.Index];
		};
		const TFunction<int32(const FVoxelLandValueSource&)> IntDefault = [&](const FVoxelLandValueSource& S) {
			return S.bParameter ? D.Parameters[S.Index].IntDefault : int32(D.Constants[S.Index]);
		};
		const float Frequency = FloatDefault(N.Frequency), Gain = FloatDefault(N.Gain),
					Lacunarity = FloatDefault(N.Lacunarity);
		const int32 Octaves = IntDefault(N.Octaves);
		if (Frequency <= 0 || Gain < 0 || Gain > 1 || Lacunarity < 1 || Octaves < 1 || Octaves > 16)
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Noise defaults out of range"));
		}
	}
	return {};
}

}

FVoxelLandError FVoxelLandProgram::Validate(
    const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
    const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
    return Validate(Draft, Providers, OutProgram);
}
FVoxelLandError FVoxelLandProgram::Validate(const FVoxelLandProgramDraft& Draft, const FVoxelLandOperatorRegistrySnapshot& Providers,
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	OutProgram.Reset();
	if (IsInGameThread())
	{
		return MakeVoxelLandError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Program validation requires a worker thread"));
	}
	if (!DraftWithinLimits(Draft))
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Program exceeds bounded input limits"));
	}
	if ((Draft.FormatVersion != 1 && Draft.FormatVersion != 2) || Draft.RegisterCount < 1 || Draft.RegisterCount > 1024)
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported version or register count"));
	}
	TSharedPtr<FVoxelLandProgram, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandProgram);
	Result->Data = Draft;
	FVoxelLandProgramDraft& D = Result->Data;
	if (FVoxelLandError Error = ValidateProgramInputs(D))
	{
		return Error;
	}
	if ((D.FormatVersion == 1 && (!D.ExternalOperators.IsEmpty() || D.ExternalFilterIndex != INDEX_NONE)) || D.ExternalOperators.Num() > 4096 ||
        (D.ExternalFilterIndex != INDEX_NONE && (!D.ExternalOperators.IsValidIndex(D.ExternalFilterIndex) || D.SmoothFilter.bPresent)))
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid external operator table"));
	}
	int64 ConfigBytes = 0;
	uint64 WorkerBytes = 0;
    for (int32 OperatorIndex = 0; OperatorIndex < D.ExternalOperators.Num(); ++OperatorIndex)
    {
        const FVoxelLandExternalOperator& Operator = D.ExternalOperators[OperatorIndex];
        if ((OperatorIndex == D.ExternalFilterIndex) != (Operator.Stage == EVoxelLandOperatorStage::OneCellFilter) ||
            (Operator.Stage != EVoxelLandOperatorStage::Sample && Operator.Stage != EVoxelLandOperatorStage::OneCellFilter))
        {
            return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid external operator stage"));
        }
		ConfigBytes += Operator.Configuration.Num();
		if (Operator.Configuration.Num() > 65536 || ConfigBytes > 1048576 || Operator.SeedSlot < 0 ||
			Operator.SeedSlot >= D.Seeds.Num() || Operator.Inputs.Num() < 1 || Operator.Inputs.Num() > 3)
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("External operator exceeds limits"));
		}
		TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
			Providers.Resolve(Operator.Id, Operator.Version);
		FString Error;
		if (!Provider || Provider->Stage() != Operator.Stage || Provider->InputTypes() != Operator.Inputs || Provider->OutputType() != Operator.Output ||
			Provider->MaximumWorkerBytes() > 1048576 || !Provider->ValidateConfiguration(Operator.Configuration, Error))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram,
				TEXT("Missing, incompatible or invalid external operator provider"));
		}
		WorkerBytes += uint64(Provider->MaximumWorkerBytes()) *
			(Provider->SimdRequirement() == EVoxelLandOperatorSimdRequirement::None ? 1u : 2u);
		if (WorkerBytes > 8u * 1024u * 1024u)
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("External operator worker budget exceeded"));
		}
		Result->Operators.Add(MoveTemp(Provider));
	}
	TArray<bool> Initialized;
	Initialized.Init(false, D.RegisterCount);
	TArray<uint8> RegisterTypes;
	RegisterTypes.Init(0, D.RegisterCount);
	int32 Stores = 0;
	for (int32 Index = 0; Index < D.Instructions.Num(); ++Index)
	{
		const FVoxelLandInstruction& I = D.Instructions[Index];
		bool A = false, B = false, C = false, ExtraD = false, ExtraE = false, ExtraF = false, ImmReg = false, Writes = true;
		uint8 TypeA = 1, TypeB = 1, TypeC = 1, WriteType = 1;
		if (D.FormatVersion == 1 && uint16(I.Op) > uint16(EVoxelLandOpcode::StoreDensity))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Version 2 opcode in version 1 program"), Index);
		}
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
			case EVoxelLandOpcode::LoadY:
			case EVoxelLandOpcode::LoadZ:
				break;
			case EVoxelLandOpcode::LoadConstant:
				if (I.Imm >= uint32(D.Constants.Num()))
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Constant out of range"), Index);
				}
				break;
			case EVoxelLandOpcode::LoadFloatParameter:
				if (I.Imm >= uint32(D.Parameters.Num()) || D.Parameters[I.Imm].Type != EVoxelLandParameterType::Float)
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Float parameter out of range or wrong type"), Index);
				}
				break;
			case EVoxelLandOpcode::Abs:
			case EVoxelLandOpcode::Negate:
				A = true;
				break;
			case EVoxelLandOpcode::Add:
			case EVoxelLandOpcode::Subtract:
			case EVoxelLandOpcode::Multiply:
			case EVoxelLandOpcode::Divide:
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Max:
			case EVoxelLandOpcode::Union:
			case EVoxelLandOpcode::SubtractShape:
			case EVoxelLandOpcode::Intersect:
				A = B = true;
				break;
			case EVoxelLandOpcode::Plane:
				A = B = C = true;
				if (uint64(I.Imm) + 4 > uint64(D.Constants.Num()))
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Plane constants out of range"), Index);
				}
				{
					const double X = D.Constants[I.Imm], Y = D.Constants[I.Imm + 1], Z = D.Constants[I.Imm + 2];
					if (FMath::Abs(X * X + Y * Y + Z * Z - 1.0) > 0.00001)
					{
						return MakeVoxelLandError(
							EVoxelLandErrorCode::InvalidProgram, TEXT("Plane normal must be unit length"), Index);
					}
				}
				break;
			case EVoxelLandOpcode::Sphere:
				A = B = C = true;
				if (I.Imm >= uint32(D.Constants.Num()) || D.Constants[I.Imm] <= 0)
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid sphere radius"), Index);
				}
				break;
			case EVoxelLandOpcode::Box:
				A = B = C = true;
				if (uint64(I.Imm) + 3 > uint64(D.Constants.Num()) || D.Constants[I.Imm] <= 0
					|| D.Constants[I.Imm + 1] <= 0 || D.Constants[I.Imm + 2] <= 0)
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid box extents"), Index);
				}
				break;
			case EVoxelLandOpcode::Perlin2:
			case EVoxelLandOpcode::Fbm2:
			case EVoxelLandOpcode::Value2:
			case EVoxelLandOpcode::Simplex2:
				A = B = true;
				if (I.Imm >= uint32(D.Noise.Num()))
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Noise config out of range"), Index);
				}
				break;
			case EVoxelLandOpcode::Perlin3:
			case EVoxelLandOpcode::Fbm3:
			case EVoxelLandOpcode::Value3:
			case EVoxelLandOpcode::Simplex3:
				A = B = C = true;
				if (I.Imm >= uint32(D.Noise.Num()))
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Noise config out of range"), Index);
				}
				break;
			case EVoxelLandOpcode::LoadIntConstant:
				WriteType = 2;
				break;
			case EVoxelLandOpcode::LoadBoolConstant:
				if (I.Imm > 1)
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Boolean constant must be 0 or 1"), Index);
				}
				WriteType = 3;
				break;
			case EVoxelLandOpcode::LoadIntParameter:
			case EVoxelLandOpcode::LoadBoolParameter:
			{
				const bool bBool = I.Op == EVoxelLandOpcode::LoadBoolParameter;
				if (I.Imm >= uint32(D.Parameters.Num()) || D.Parameters[I.Imm].Type !=
					(bBool ? EVoxelLandParameterType::Bool : EVoxelLandParameterType::Int32))
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid typed parameter"), Index);
				}
				WriteType = bBool ? 3 : 2;
				break;
			}
			case EVoxelLandOpcode::IntToFloat:
				A = true; TypeA = 2; break;
			case EVoxelLandOpcode::Less:
			case EVoxelLandOpcode::LessEqual:
			case EVoxelLandOpcode::Equal:
				A = B = true; WriteType = 3; break;
			case EVoxelLandOpcode::BoolAnd:
			case EVoxelLandOpcode::BoolOr:
				A = B = true; TypeA = TypeB = WriteType = 3; break;
			case EVoxelLandOpcode::BoolNot:
				A = true; TypeA = WriteType = 3; break;
			case EVoxelLandOpcode::SelectFloat:
				A = B = C = true; TypeA = 3; break;
			case EVoxelLandOpcode::SelectInt:
				A = B = C = true; TypeA = 3; TypeB = TypeC = WriteType = 2; break;
			case EVoxelLandOpcode::SelectBool:
				A = B = C = true; TypeA = TypeB = TypeC = WriteType = 3; break;
			case EVoxelLandOpcode::Power:
				A = B = true; break;
			case EVoxelLandOpcode::Clamp:
				A = B = C = true; break;
			case EVoxelLandOpcode::DynamicPlane:
				A = B = C = ExtraD = ExtraE = ExtraF = ImmReg = true; break;
			case EVoxelLandOpcode::DynamicSphere:
				A = B = C = ExtraD = true; break;
			case EVoxelLandOpcode::DynamicBox:
				A = B = C = ExtraD = ExtraE = ExtraF = true; break;
			case EVoxelLandOpcode::ExternalOperator:
			{
				if (I.Imm >= uint32(D.ExternalOperators.Num()))
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("External operator index out of range"), Index);
				}
				const FVoxelLandExternalOperator& Operator = D.ExternalOperators[I.Imm];
                if (Operator.Stage != EVoxelLandOperatorStage::Sample)
                {
                    return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Filter cannot execute as a sample operator"), Index);
                }
				A = true;
				B = Operator.Inputs.Num() > 1;
				C = Operator.Inputs.Num() > 2;
				if (I.Dst == I.A || (B && I.Dst == I.B) || (C && I.Dst == I.C))
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("External output must not alias an input"), Index);
				}
				TypeA = uint8(Operator.Inputs[0]) + 1;
				if (B) { TypeB = uint8(Operator.Inputs[1]) + 1; }
				if (C) { TypeC = uint8(Operator.Inputs[2]) + 1; }
				WriteType = uint8(Operator.Output) + 1;
				break;
			}
			case EVoxelLandOpcode::StoreDensity:
				A = true;
				Writes = false;
				++Stores;
				break;
			default:
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Unknown opcode"), Index);
		}
		if ((A && (I.A >= D.RegisterCount || !Initialized[I.A])) || (B && (I.B >= D.RegisterCount || !Initialized[I.B]))
			|| (C && (I.C >= D.RegisterCount || !Initialized[I.C]))
            || (ExtraD && (I.D >= D.RegisterCount || !Initialized[I.D]))
            || (ExtraE && (I.E >= D.RegisterCount || !Initialized[I.E]))
            || (ExtraF && (I.F >= D.RegisterCount || !Initialized[I.F]))
            || (ImmReg && (I.Imm >= uint32(D.RegisterCount) || !Initialized[I.Imm]))
            || (Writes && I.Dst >= D.RegisterCount))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram,
				TEXT("Register out of range or read before initialization"), Index);
		}
		if ((A && RegisterTypes[I.A] != TypeA) || (B && RegisterTypes[I.B] != TypeB) ||
			(C && RegisterTypes[I.C] != TypeC) || (ExtraD && RegisterTypes[I.D] != 1) ||
            (ExtraE && RegisterTypes[I.E] != 1) || (ExtraF && RegisterTypes[I.F] != 1) ||
            (ImmReg && RegisterTypes[I.Imm] != 1))
		{
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Register type mismatch"), Index);
		}
		if (Writes)
		{
			RegisterTypes[I.Dst] = WriteType;
			Initialized[I.Dst] = true;
		}
	}
	if (Stores != 1)
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Exactly one density store is required"));
	}
	Result->Fingerprint = ComputeProgramFingerprint(D);
	OutProgram = Result;
	return {};
}

namespace VoxelLandDeterminism
{
static uint64 Mix64(uint64 X)
{
	X += 0x9E3779B97F4A7C15ull;
	X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
	X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
	return X ^ (X >> 31);
}
uint64 DeriveNodeSeed(uint64 WorldSeed, const FGuid& G, uint32 Salt)
{
	const uint64 H = Mix64((uint64(G.A) << 32 | G.B) ^ Mix64(uint64(G.C) << 32 | G.D));
	return Mix64(WorldSeed ^ Mix64(H + Salt));
}
FVoxelLandError Quantize(float DistanceCm, float VoxelSizeCm, int16& OutDensity)
{
	if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(VoxelSizeCm) || VoxelSizeCm <= 0)
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Invalid density or voxel size"));
	}
	const double Scaled = (double(DistanceCm) / double(VoxelSizeCm)) * 1024.0;
	if (!FMath::IsFinite(Scaled))
	{
		return MakeVoxelLandError(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite scaled density"));
	}
	if (Scaled <= -32768.0)
	{
		OutDensity = -32768;
	}
	else if (Scaled >= 32767.0)
	{
		OutDensity = 32767;
	}
	else
	{
		OutDensity = int16(Scaled >= 0 ? FMath::FloorToDouble(Scaled + 0.5) : FMath::CeilToDouble(Scaled - 0.5));
	}
	return {};
}
uint64 HashDensity(const TArray<int16>& Density)
{
	TArray<uint8> Bytes;
	Bytes.Reserve(Density.Num() * 2);
	for (int16 Value : Density)
	{
		const uint16 Bits = uint16(Value);
		Bytes.Add(uint8(Bits));
		Bytes.Add(uint8(Bits >> 8));
	}
	return FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
}
}
