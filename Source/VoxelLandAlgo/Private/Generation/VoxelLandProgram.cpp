#include "Generation/VoxelLandGenerator.h"
#include "Hash/xxhash.h"

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
		return false;
	Out.Reset();
	for (TCHAR C : In)
	{
		if (C >= 'A' && C <= 'Z')
			C += 'a' - 'A';
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
			return false;
		Out.AppendChar(C);
	}
	return true;
}

struct FCanonicalWriter
{
	TArray<uint8> Bytes;
	void U8(uint8 V) { Bytes.Add(V); }
	void U16(uint16 V)
	{
		U8(uint8(V));
		U8(uint8(V >> 8));
	}
	void U32(uint32 V)
	{
		for (int32 I = 0; I < 4; ++I)
			U8(uint8(V >> (I * 8)));
	}
	void Float(float V)
	{
		uint32 Bits = 0;
		if (V != 0.0f)
			FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		U32(Bits);
	}
	void String(const FString& S)
	{
		FTCHARToUTF8 Utf8(*S);
		U32(uint32(Utf8.Length()));
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	void Source(const FVoxelLandValueSource& S)
	{
		U8(S.bParameter ? 1 : 0);
		U32(S.Index);
	}
};

uint64 Fingerprint(const FVoxelLandProgramDraft& D)
{
	FCanonicalWriter W;
	W.U32(D.FormatVersion);
	W.U32(uint32(D.RegisterCount));
	W.U32(D.Instructions.Num());
	for (const auto& I : D.Instructions)
	{
		W.U16(uint16(I.Op));
		W.U16(I.Dst);
		W.U16(I.A);
		W.U16(I.B);
		W.U16(I.C);
		W.U32(I.Imm);
	}
	W.U32(D.Constants.Num());
	for (float V : D.Constants)
		W.Float(V);
	W.U32(D.Parameters.Num());
	for (const auto& P : D.Parameters)
	{
		W.String(P.Name);
		W.U8(uint8(P.Type));
		W.Float(P.FloatDefault);
		W.U32(uint32(P.IntDefault));
	}
	W.U32(D.Seeds.Num());
	for (const auto& S : D.Seeds)
	{
		W.U32(S.NodeId.A);
		W.U32(S.NodeId.B);
		W.U32(S.NodeId.C);
		W.U32(S.NodeId.D);
		W.U32(S.Salt);
	}
	W.U32(D.Noise.Num());
	for (const auto& N : D.Noise)
	{
		W.U32(N.SeedSlot);
		W.Source(N.Frequency);
		W.Source(N.Octaves);
		W.Source(N.Gain);
		W.Source(N.Lacunarity);
	}
	return FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
}

bool SourceValid(const FVoxelLandProgramDraft& D, const FVoxelLandValueSource& Source, EVoxelLandParameterType Type)
{
	if (Source.bParameter)
		return Source.Index < uint32(D.Parameters.Num()) && D.Parameters[Source.Index].Type == Type;
	if (Source.Index >= uint32(D.Constants.Num()))
		return false;
	const float V = D.Constants[Source.Index];
	return Type == EVoxelLandParameterType::Float || (V >= 1.f && V <= 16.f && FMath::FloorToFloat(V) == V);
}
}

FVoxelLandError FVoxelLandProgram::Validate(
	const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	if (IsInGameThread())
		return MakeVoxelLandError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Program validation requires a worker thread"));
	OutProgram.Reset();
	if (Draft.FormatVersion != 1 || Draft.RegisterCount < 1 || Draft.RegisterCount > 1024)
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported version or register count"));
	TSharedPtr<FVoxelLandProgram, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandProgram);
	Result->Data = Draft;
	auto& D = Result->Data;
	for (float V : D.Constants)
		if (!FMath::IsFinite(V))
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Non-finite constant"));
	TSet<FString> Names;
	for (auto& P : D.Parameters)
	{
		FString Name;
		if (!NormalizeName(P.Name, Name) || Names.Contains(Name)
			|| (P.Type != EVoxelLandParameterType::Float && P.Type != EVoxelLandParameterType::Int32)
			|| !FMath::IsFinite(P.FloatDefault))
			return MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate parameter"), INDEX_NONE, P.Name);
		P.Name = Name;
		Names.Add(Name);
	}
	for (int32 I = 0; I < D.Seeds.Num(); ++I)
	{
		const auto& Seed = D.Seeds[I];
		if (!Seed.NodeId.IsValid())
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid seed GUID"));
		for (int32 J = 0; J < I; ++J)
			if (D.Seeds[J].NodeId == Seed.NodeId && D.Seeds[J].Salt == Seed.Salt)
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Duplicate seed GUID and salt"));
	}
	for (const auto& N : D.Noise)
	{
		if (N.SeedSlot >= uint32(D.Seeds.Num()) || !SourceValid(D, N.Frequency, EVoxelLandParameterType::Float)
			|| !SourceValid(D, N.Octaves, EVoxelLandParameterType::Int32)
			|| !SourceValid(D, N.Gain, EVoxelLandParameterType::Float)
			|| !SourceValid(D, N.Lacunarity, EVoxelLandParameterType::Float))
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid noise configuration source"));
		const auto FloatDefault = [&](const FVoxelLandValueSource& S) {
			return S.bParameter ? D.Parameters[S.Index].FloatDefault : D.Constants[S.Index];
		};
		const auto IntDefault = [&](const FVoxelLandValueSource& S) {
			return S.bParameter ? D.Parameters[S.Index].IntDefault : int32(D.Constants[S.Index]);
		};
		const float Frequency = FloatDefault(N.Frequency), Gain = FloatDefault(N.Gain),
					Lacunarity = FloatDefault(N.Lacunarity);
		const int32 Octaves = IntDefault(N.Octaves);
		if (Frequency <= 0 || Gain < 0 || Gain > 1 || Lacunarity < 1 || Octaves < 1 || Octaves > 16)
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Noise defaults out of range"));
	}
	TArray<bool> Initialized;
	Initialized.Init(false, D.RegisterCount);
	int32 Stores = 0;
	for (int32 Index = 0; Index < D.Instructions.Num(); ++Index)
	{
		const auto& I = D.Instructions[Index];
		bool A = false, B = false, C = false, Writes = true;
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
			case EVoxelLandOpcode::LoadY:
			case EVoxelLandOpcode::LoadZ:
				break;
			case EVoxelLandOpcode::LoadConstant:
				if (I.Imm >= uint32(D.Constants.Num()))
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Constant out of range"), Index);
				break;
			case EVoxelLandOpcode::LoadFloatParameter:
				if (I.Imm >= uint32(D.Parameters.Num()) || D.Parameters[I.Imm].Type != EVoxelLandParameterType::Float)
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Float parameter out of range or wrong type"), Index);
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
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Plane constants out of range"), Index);
				{
					const double X = D.Constants[I.Imm], Y = D.Constants[I.Imm + 1], Z = D.Constants[I.Imm + 2];
					if (FMath::Abs(X * X + Y * Y + Z * Z - 1.0) > 0.00001)
						return MakeVoxelLandError(
							EVoxelLandErrorCode::InvalidProgram, TEXT("Plane normal must be unit length"), Index);
				}
				break;
			case EVoxelLandOpcode::Sphere:
				A = B = C = true;
				if (I.Imm >= uint32(D.Constants.Num()) || D.Constants[I.Imm] <= 0)
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid sphere radius"), Index);
				break;
			case EVoxelLandOpcode::Box:
				A = B = C = true;
				if (uint64(I.Imm) + 3 > uint64(D.Constants.Num()) || D.Constants[I.Imm] <= 0
					|| D.Constants[I.Imm + 1] <= 0 || D.Constants[I.Imm + 2] <= 0)
					return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid box extents"), Index);
				break;
			case EVoxelLandOpcode::Perlin2:
			case EVoxelLandOpcode::Fbm2:
				A = B = true;
				if (I.Imm >= uint32(D.Noise.Num()))
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Noise config out of range"), Index);
				break;
			case EVoxelLandOpcode::Perlin3:
			case EVoxelLandOpcode::Fbm3:
				A = B = C = true;
				if (I.Imm >= uint32(D.Noise.Num()))
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Noise config out of range"), Index);
				break;
			case EVoxelLandOpcode::StoreDensity:
				A = true;
				Writes = false;
				++Stores;
				break;
			default:
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Unknown opcode"), Index);
		}
		if ((A && (I.A >= D.RegisterCount || !Initialized[I.A])) || (B && (I.B >= D.RegisterCount || !Initialized[I.B]))
			|| (C && (I.C >= D.RegisterCount || !Initialized[I.C])) || (Writes && I.Dst >= D.RegisterCount))
			return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram,
				TEXT("Register out of range or read before initialization"), Index);
		if (Writes)
			Initialized[I.Dst] = true;
	}
	if (Stores != 1)
		return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Exactly one density store is required"));
	Result->Fingerprint = Fingerprint(D);
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
		return MakeVoxelLandError(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Invalid density or voxel size"));
	const double Scaled = (double(DistanceCm) / double(VoxelSizeCm)) * 1024.0;
	if (!FMath::IsFinite(Scaled))
		return MakeVoxelLandError(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite scaled density"));
	if (Scaled <= -32768.0)
		OutDensity = -32768;
	else if (Scaled >= 32767.0)
		OutDensity = 32767;
	else
		OutDensity = int16(Scaled >= 0 ? FMath::FloorToDouble(Scaled + 0.5) : FMath::CeilToDouble(Scaled - 0.5));
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
