#include "World/VoxelLandWorldProgramBytes.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"

class FVoxelLandWorldProgramBytesInternals final
{
public:
	static FVoxelLandM5Diagnostic Failure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("World.ProgramBytes");
		Result.Message = Message;
		return Result;
	}

	static void U64(FCanonicalWriter& Writer, uint64 Value)
	{
		Writer.U32(uint32(Value));
		Writer.U32(uint32(Value >> 32));
	}

	static void Guid(FCanonicalWriter& Writer, const FGuid& Value)
	{
		Writer.U32(Value.A);
		Writer.U32(Value.B);
		Writer.U32(Value.C);
		Writer.U32(Value.D);
	}

	static void Source(FCanonicalWriter& Writer, const FVoxelLandSerializedValueSource& Value)
	{
		Writer.U8(Value.bParameter ? 1 : 0);
		Writer.U32(uint32(Value.Index));
	}
};

FVoxelLandWorldProgramBytes::FVoxelLandWorldProgramBytes(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease, TArray<uint8>&& InBytes)
	: Budget(MoveTemp(InBudget))
	, Lease(MoveTemp(InLease))
	, Bytes(MoveTemp(InBytes))
{
}

FVoxelLandM5Diagnostic FVoxelLandWorldProgramBytes::Encode(const FVoxelLandSerializedProgram& Program,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandWorldProgramBytes, ESPMode::ThreadSafe>& OutBytes)
{
	if (IsCancelled())
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled."));
	}
	if (Program.Instructions.Num() > 8192 || Program.Constants.Num() > 65536 || Program.Parameters.Num() > 4096
		|| Program.Seeds.Num() > 4096 || Program.Noise.Num() > 4096 || Program.ExternalOperators.Num() > 128)
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Program envelope exceeds encoding limits."));
	}
	const int64 Records = int64(Program.Instructions.Num()) + Program.Constants.Num() + Program.Parameters.Num()
		+ Program.Seeds.Num() + Program.Noise.Num() + Program.ExternalOperators.Num();
	if (!Budget->TryChargeWork(Records + 1))
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program preflight work exceeds budget."));
	}
	int64 VariableBytes = 0;
	for (const FVoxelLandSerializedParameter& Parameter : Program.Parameters)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled."));
		}
		if (Parameter.Name.Len() > 256 || !FMath::IsFinite(Parameter.FloatDefault))
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid program parameter."));
		}
		VariableBytes += Parameter.Name.Len();
		for (const TCHAR Character : Parameter.Name)
		{
			if (Character <= 0 || Character > 127)
			{
				return FVoxelLandWorldProgramBytesInternals::Failure(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Only ASCII program parameter names are supported."));
			}
		}
	}
	for (const float Value : Program.Constants)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled."));
		}
		if (!FMath::IsFinite(Value))
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Nonfinite program constant."));
		}
	}
	for (const FVoxelLandExternalOperator& Operator : Program.ExternalOperators)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled."));
		}
		if (Operator.Inputs.Num() > 64 || Operator.Configuration.Num() > 65536
			|| VariableBytes > 2097152 - Operator.Inputs.Num() - Operator.Configuration.Num())
		{
			return FVoxelLandWorldProgramBytesInternals::Failure(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Provider configuration exceeds encoding limits."));
		}
		VariableBytes += Operator.Inputs.Num() + Operator.Configuration.Num();
	}
	const int64 ReservedBytes = 4096 + Records * 128 + VariableBytes * 2;
	if (!Budget->TryChargeWork(Records * 128 + VariableBytes * 4 + 4096))
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program encoding work exceeds budget."));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ReservedBytes, Lease))
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program encoding memory exceeds budget."));
	}
	FCanonicalWriter W;
	W.Bytes.Reserve(int32(ReservedBytes));
	W.U8('V'); W.U8('P'); W.U8('G'); W.U8('M');
	W.U32(1);
	W.U32(uint32(Program.EnvelopeVersion));
	W.U32(uint32(Program.ProgramVersion));
	W.U32(uint32(Program.CompilerSemanticVersion));
	W.U32(uint32(Program.ExternalFilterIndex));
	W.U32(uint32(Program.RegisterCount));
	FVoxelLandWorldProgramBytesInternals::U64(W, uint64(Program.SourceFingerprint));
	FVoxelLandWorldProgramBytesInternals::U64(W, uint64(Program.ProgramFingerprint));
	W.U32(uint32(Program.Instructions.Num()));
	for (const FVoxelLandSerializedInstruction& I : Program.Instructions)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		W.U32(uint32(I.Opcode)); W.U32(uint32(I.Dst)); W.U32(uint32(I.A)); W.U32(uint32(I.B));
		W.U32(uint32(I.C)); W.U32(uint32(I.D)); W.U32(uint32(I.E)); W.U32(uint32(I.F)); W.U32(uint32(I.Immediate));
	}
	W.U32(uint32(Program.Constants.Num()));
	for (const float Value : Program.Constants)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		W.Float(Value);
	}
	W.U32(uint32(Program.Parameters.Num()));
	for (const FVoxelLandSerializedParameter& P : Program.Parameters)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		FVoxelLandWorldProgramBytesInternals::Guid(W, P.Id);
		W.U32(uint32(P.Name.Len()));
		for (const TCHAR Character : P.Name) { W.U8(uint8(Character)); }
		W.U8(P.Type); W.Float(P.FloatDefault); W.U32(uint32(P.IntDefault));
	}
	W.U32(uint32(Program.Seeds.Num()));
	for (const FVoxelLandSerializedSeed& S : Program.Seeds)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		FVoxelLandWorldProgramBytesInternals::Guid(W, S.NodeId); W.U32(uint32(S.Salt));
	}
	W.U32(uint32(Program.Noise.Num()));
	for (const FVoxelLandSerializedNoise& N : Program.Noise)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		W.U32(uint32(N.SeedSlot));
		FVoxelLandWorldProgramBytesInternals::Source(W, N.Frequency);
		FVoxelLandWorldProgramBytesInternals::Source(W, N.Octaves);
		FVoxelLandWorldProgramBytesInternals::Source(W, N.Gain);
		FVoxelLandWorldProgramBytesInternals::Source(W, N.Lacunarity);
	}
	W.U32(uint32(Program.ExternalOperators.Num()));
	for (const FVoxelLandExternalOperator& Operator : Program.ExternalOperators)
	{
		if (IsCancelled()) { return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled.")); }
		FVoxelLandWorldProgramBytesInternals::Guid(W, Operator.Id);
		W.U32(uint32(Operator.Version)); W.U8(uint8(Operator.Stage)); W.U8(uint8(Operator.Output));
		W.U32(uint32(Operator.SeedSlot)); W.U32(uint32(Operator.Inputs.Num()));
		for (const EVoxelLandGraphValueType Input : Operator.Inputs) { W.U8(uint8(Input)); }
		W.U32(uint32(Operator.Configuration.Num()));
		W.Bytes.Append(Operator.Configuration);
	}
	W.U8(Program.SmoothFilter.bPresent ? 1 : 0);
	FVoxelLandWorldProgramBytesInternals::Source(W, Program.SmoothFilter.Enabled);
	FVoxelLandWorldProgramBytesInternals::Source(W, Program.SmoothFilter.Strength);
	if (IsCancelled())
	{
		return FVoxelLandWorldProgramBytesInternals::Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled."));
	}
	OutBytes = MakeShareable(new FVoxelLandWorldProgramBytes(Budget, MoveTemp(Lease), MoveTemp(W.Bytes)));
	return {};
}
