#include "Graph/Serialization/VoxelLandCaveProgramDecoder.h"

#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Program/Serialization/VoxelLandSerializedProgram.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Graph/Serialization/VoxelLandCaveDecodedDraft.h"
#include "Graph/Serialization/VoxelLandCaveSourceFingerprint.h"

class FVoxelLandM5ProgramDecoderInternals final
{
public:
	static FVoxelLandM5Diagnostic M5DecoderError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static uint64 M5DecoderUnsignedBits(int64 Value);
	static FVoxelLandValueSource M5DecoderValueSource(const FVoxelLandSerializedValueSource& Value);
	static bool M5DecoderInstructionRange(const FVoxelLandSerializedInstruction& Record);
	static FVoxelLandM5Diagnostic M5DecoderValidateFunctionBounds(const FVoxelLandGraphSnapshot& Source,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
};

FVoxelLandM5Diagnostic FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.ProgramDecode");
	Result.Message = Message;
	return Result;
}

uint64 FVoxelLandM5ProgramDecoderInternals::M5DecoderUnsignedBits(int64 Value)
{
	uint64 Result = 0;
	FMemory::Memcpy(&Result, &Value, sizeof(Result));
	return Result;
}

FVoxelLandValueSource FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(
	const FVoxelLandSerializedValueSource& Value)
{
	FVoxelLandValueSource Result;
	Result.bParameter = Value.bParameter;
	Result.Index = uint32(Value.Index);
	return Result;
}

bool FVoxelLandM5ProgramDecoderInternals::M5DecoderInstructionRange(const FVoxelLandSerializedInstruction& Record)
{
	return Record.Opcode >= 0 && Record.Opcode <= int32(EVoxelLandOpcode::ExternalOperator) && Record.Dst >= 0
		&& Record.Dst <= 65535 && Record.A >= 0 && Record.A <= 65535 && Record.B >= 0 && Record.B <= 65535
		&& Record.C >= 0 && Record.C <= 65535 && Record.D >= 0 && Record.D <= 65535 && Record.E >= 0
		&& Record.E <= 65535 && Record.F >= 0 && Record.F <= 65535;
}

// Check function-source bounds without copying graphs or accessing tooltip text.
// SourceFingerprint reads parameter fields by reference and excludes FText.
FVoxelLandM5Diagnostic FVoxelLandM5ProgramDecoderInternals::M5DecoderValidateFunctionBounds(
	const FVoxelLandGraphSnapshot& Source, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (Source.Functions.IsEmpty())
	{
		return {};
	}
	if (Source.Functions.Num() > 128)
	{
		return M5DecoderError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function count exceeds limit"));
	}
	if (!Budget->TryChargeWork(int64(Source.Functions.Num()) * 2 + 1))
	{
		return M5DecoderError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function validation work exceeds budget"));
	}
	int64 TotalNodes = Source.Nodes.Num();
	int64 TotalLinks = Source.Links.Num();
	if (TotalNodes > 4096 || TotalLinks > 16384)
	{
		return M5DecoderError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Function graph exceeds legacy limits"));
	}
	for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
	{
		if (IsCancelled())
		{
			return M5DecoderError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (Function.SourceVersion != 1 || Function.Inputs.Num() > 63
			|| !TryAdd(TotalNodes, Function.Nodes.Num(), TotalNodes)
			|| !TryAdd(TotalLinks, Function.Links.Num(), TotalLinks) || TotalNodes > 4096 || TotalLinks > 16384)
		{
			return M5DecoderError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Captured function exceeds legacy limits"));
		}
	}
	int64 TotalConfiguration = 0;
	const auto CheckNodes = [&Budget, &IsCancelled, &TotalConfiguration](
								const TArray<FVoxelLandGraphNodeRecord>& Nodes) -> FVoxelLandM5Diagnostic {
		for (const FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			if (!Budget->TryChargeWork(3))
			{
				return M5DecoderError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function node work exceeds budget"));
			}
			if (IsCancelled())
			{
				return M5DecoderError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (Node.Pins.Num() > 64 || Node.Configuration.Num() > 65536
				|| !TryAdd(TotalConfiguration, Node.Configuration.Num(), TotalConfiguration)
				|| TotalConfiguration > 1048576)
			{
				return M5DecoderError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Function node exceeds legacy limits"));
			}
			if (!Budget->TryChargeWork(int64(Node.Pins.Num()) * 2))
			{
				return M5DecoderError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function pin work exceeds budget"));
			}
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (IsCancelled())
				{
					return M5DecoderError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
				}
				if (Pin.Name.Len() > 128)
				{
					return M5DecoderError(
						EVoxelLandM5ErrorCode::InvalidInput, TEXT("Function pin name exceeds legacy limit"));
				}
			}
		}
		return {};
	};
	FVoxelLandM5Diagnostic D = CheckNodes(Source.Nodes);
	if (D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
	{
		D = CheckNodes(Function.Nodes);
		if (D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
	}
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5ProgramDecoder::Decode(const FVoxelLandGraphSnapshot& Source,
	const FVoxelLandSerializedProgram& Payload, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe>& OutDraft)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (Payload.EnvelopeVersion != 1 || Payload.ProgramVersion != int32(FVoxelLandProgramDraft::CurrentFormatVersion)
		|| Payload.CompilerSemanticVersion != int32(FVoxelLandProgramDraft::CompilerSemanticVersion))
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::UnsupportedVersion, TEXT("Unsupported program envelope or compiler version"));
	}
	if (Payload.RegisterCount < 1 || Payload.RegisterCount > 1024 || Payload.Instructions.Num() > 8192
		|| Payload.Constants.Num() > 65536 || Payload.Parameters.Num() > 4096 || Payload.Seeds.Num() > 4096
		|| Payload.Noise.Num() > 4096 || Source.Parameters.Num() > 4096)
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program exceeds decode limits"));
	}
	if (!Payload.ExternalOperators.IsEmpty() || Payload.ExternalFilterIndex != INDEX_NONE)
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("External provider decoding is not metered"));
	}
	const int64 Counts = int64(Payload.Instructions.Num()) + Payload.Constants.Num() + Payload.Parameters.Num()
					   + Payload.Seeds.Num() + Payload.Noise.Num() + Source.Parameters.Num();
	// The nested parameter matching loops, their cancellation polls, and
	// byte-wise bounded parameter-name comparison are conservatively prepaid.
	int64 Matches = 0;
	int64 MatchWork = 0;
	int64 DuplicateWork = 0;
	int64 NameWork = 0;
	int64 Work = 0;
	if (!TryMultiplyNonNegative(Payload.Parameters.Num(), Source.Parameters.Num(), Matches)
		|| !TryMultiplyNonNegative(Matches, 260, MatchWork)
		|| !TryMultiplyNonNegative(Payload.Parameters.Num(), Payload.Parameters.Num(), DuplicateWork)
		|| !TryMultiplyNonNegative(DuplicateWork, 2, DuplicateWork)
		|| !TryMultiplyNonNegative(Payload.Parameters.Num(), 512, NameWork) || !TryMultiplyNonNegative(Counts, 8, Work)
		|| !TryAdd(Work, MatchWork, Work) || !TryAdd(Work, DuplicateWork, Work) || !TryAdd(Work, NameWork, Work)
		|| !TryAdd(Work, 64, Work) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program decode work exceeds budget"));
	}
	int64 NameBytes = 0;
	for (const FVoxelLandSerializedParameter& P : Payload.Parameters)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (P.Name.Len() > 256 || !TryAdd(NameBytes, (int64(P.Name.Len()) + 1) * sizeof(TCHAR), NameBytes))
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Parameter name exceeds decode limits"));
		}
		for (int32 I = 0; I < P.Name.Len(); ++I)
		{
			if ((I & 63) == 0 && IsCancelled())
			{
				return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (P.Name[I] <= 0 || P.Name[I] > 127)
			{
				return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Non-ASCII parameter names are not metered"));
			}
		}
	}
	if (FVoxelLandM5Diagnostic D =
			FVoxelLandM5ProgramDecoderInternals::M5DecoderValidateFunctionBounds(Source, Budget, IsCancelled);
		D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	uint64 SourceHash = 0;
	if (FVoxelLandM5Diagnostic D = VoxelLand::M5::SourceFingerprint::Compute(Source, Budget, IsCancelled, SourceHash);
		D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	if (SourceHash != FVoxelLandM5ProgramDecoderInternals::M5DecoderUnsignedBits(Payload.SourceFingerprint))
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Compiled source fingerprint mismatch"));
	}
	int64 PayloadBytes = 0;
	int64 ItemBytes = 0;
	const auto AddArrayBytes = [&PayloadBytes, &ItemBytes](int64 Count, int64 ElementSize) {
		return TryMultiplyNonNegative(Count, ElementSize, ItemBytes) && TryAdd(PayloadBytes, ItemBytes, PayloadBytes);
	};
	if (!AddArrayBytes(Payload.Instructions.Num(), sizeof(FVoxelLandInstruction))
		|| !AddArrayBytes(Payload.Constants.Num(), sizeof(float))
		|| !AddArrayBytes(Payload.Parameters.Num(), sizeof(FVoxelLandParameterSlot))
		|| !AddArrayBytes(Payload.Seeds.Num(), sizeof(FVoxelLandSeedSlot))
		|| !AddArrayBytes(Payload.Noise.Num(), sizeof(FVoxelLandNoiseConfig))
		|| !TryAdd(PayloadBytes, NameBytes, PayloadBytes) || !TryMultiplyNonNegative(PayloadBytes, 4, PayloadBytes)
		|| !TryAdd(PayloadBytes, 4096, PayloadBytes))
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program decode memory exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(PayloadBytes, Lease))
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program decode memory exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5DecodedDraft(Budget, Lease));
	FVoxelLandProgramDraft& Draft = Result->Draft;
	Draft.FormatVersion = uint32(Payload.ProgramVersion);
	Draft.RegisterCount = Payload.RegisterCount;
	Draft.ExternalFilterIndex = INDEX_NONE;
	Draft.SmoothFilter.bPresent = Payload.SmoothFilter.bPresent;
	Draft.SmoothFilter.Enabled =
		FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Payload.SmoothFilter.Enabled);
	Draft.SmoothFilter.Strength =
		FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Payload.SmoothFilter.Strength);
	Draft.Instructions.Reserve(Payload.Instructions.Num());
	for (const FVoxelLandSerializedInstruction& Record : Payload.Instructions)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (!FVoxelLandM5ProgramDecoderInternals::M5DecoderInstructionRange(Record))
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid serialized instruction"));
		}
		if (Record.Opcode == int32(EVoxelLandOpcode::ExternalOperator))
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Unproven, TEXT("External operators are not metered"));
		}
		FVoxelLandInstruction I;
		I.Op = EVoxelLandOpcode(Record.Opcode);
		I.Dst = uint16(Record.Dst);
		I.A = uint16(Record.A);
		I.B = uint16(Record.B);
		I.C = uint16(Record.C);
		I.D = uint16(Record.D);
		I.E = uint16(Record.E);
		I.F = uint16(Record.F);
		I.Imm = uint32(Record.Immediate);
		Draft.Instructions.Add(I);
	}
	Draft.Constants.Reserve(Payload.Constants.Num());
	for (int32 I = 0; I < Payload.Constants.Num(); ++I)
	{
		if ((I & 255) == 0 && IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		Draft.Constants.Add(Payload.Constants[I]);
	}
	Draft.Parameters.Reserve(Payload.Parameters.Num());
	for (int32 I = 0; I < Payload.Parameters.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandSerializedParameter& Record = Payload.Parameters[I];
		const FVoxelLandGraphParameter* Match = nullptr;
		for (const FVoxelLandGraphParameter& Parameter : Source.Parameters)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (Parameter.Id == Record.Id)
			{
				Match = &Parameter;
				break;
			}
		}
		for (int32 Earlier = 0; Earlier < I; ++Earlier)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (Payload.Parameters[Earlier].Id == Record.Id)
			{
				return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate serialized parameter ID"));
			}
		}
		if (!Match || !Match->Name.Equals(Record.Name, ESearchCase::IgnoreCase) || uint8(Match->Type) != Record.Type)
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Parameter mapping does not match source"));
		}
		FVoxelLandParameterSlot Parameter;
		Parameter.Name = Record.Name;
		Parameter.Type = EVoxelLandParameterType(Record.Type);
		Parameter.FloatDefault = Record.FloatDefault;
		Parameter.IntDefault = Record.IntDefault;
		Draft.Parameters.Add(MoveTemp(Parameter));
	}
	Draft.Seeds.Reserve(Payload.Seeds.Num());
	for (const FVoxelLandSerializedSeed& Record : Payload.Seeds)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		FVoxelLandSeedSlot Seed;
		Seed.NodeId = Record.NodeId;
		Seed.Salt = uint32(Record.Salt);
		Draft.Seeds.Add(Seed);
	}
	Draft.Noise.Reserve(Payload.Noise.Num());
	for (const FVoxelLandSerializedNoise& Record : Payload.Noise)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		FVoxelLandNoiseConfig Noise;
		Noise.SeedSlot = uint32(Record.SeedSlot);
		Noise.Frequency = FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Record.Frequency);
		Noise.Octaves = FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Record.Octaves);
		Noise.Gain = FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Record.Gain);
		Noise.Lacunarity = FVoxelLandM5ProgramDecoderInternals::M5DecoderValueSource(Record.Lacunarity);
		Draft.Noise.Add(Noise);
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ProgramDecoderInternals::M5DecoderError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	Result->ExpectedProgramFingerprint =
		FVoxelLandM5ProgramDecoderInternals::M5DecoderUnsignedBits(Payload.ProgramFingerprint);
	OutDraft = MoveTemp(Result);
	return {};
}
