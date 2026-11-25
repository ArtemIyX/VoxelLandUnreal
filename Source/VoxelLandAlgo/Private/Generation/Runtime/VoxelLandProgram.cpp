#include "Generation/Field/VoxelLandGenerator.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Hash/xxhash.h"
#include "Operators/VoxelLandOperator.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"

class FVoxelLandProgramInternals final
{
public:
	static FVoxelLandError MakeVoxelLandError(
		EVoxelLandErrorCode Code, const TCHAR* Message, int32 Instruction = INDEX_NONE, const FString& Parameter = {})
	{
		FVoxelLandError Result;
		Result.Code = Code;
		Result.Message = Message;
		Result.Instruction = Instruction;
		Result.Parameter = Parameter;
		return Result;
	}

	static bool NormalizeName(const FString& In, FString& Out)
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

	static bool ComputeProgramFingerprint(
		const FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled, bool bMetered, uint64& OutFingerprint)
	{
		FCanonicalWriter W;
		W.U32(D.FormatVersion);
		W.U32(FVoxelLandProgramDraft::CompilerSemanticVersion);
		W.U32(uint32(D.RegisterCount));
		W.U32(D.Instructions.Num());
		for (const FVoxelLandInstruction& I : D.Instructions)
		{
			if (bMetered && IsCancelled())
			{
				return false;
			}
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
		W.U32(D.Constants.Num());
		for (float V : D.Constants)
		{
			if (bMetered && IsCancelled())
			{
				return false;
			}
			W.Float(V);
		}
		W.U32(D.Parameters.Num());
		for (const FVoxelLandParameterSlot& P : D.Parameters)
		{
			if (bMetered && IsCancelled())
			{
				return false;
			}
			W.String(P.Name);
			W.U8(uint8(P.Type));
			W.Float(P.FloatDefault);
			W.U32(uint32(P.IntDefault));
		}
		W.U32(D.Seeds.Num());
		for (const FVoxelLandSeedSlot& S : D.Seeds)
		{
			if (bMetered && IsCancelled())
			{
				return false;
			}
			W.U32(S.NodeId.A);
			W.U32(S.NodeId.B);
			W.U32(S.NodeId.C);
			W.U32(S.NodeId.D);
			W.U32(S.Salt);
		}
		W.U32(D.Noise.Num());
		for (const FVoxelLandNoiseConfig& N : D.Noise)
		{
			if (bMetered && IsCancelled())
			{
				return false;
			}
			W.U32(N.SeedSlot);
			W.Source(N.Frequency);
			W.Source(N.Octaves);
			W.Source(N.Gain);
			W.Source(N.Lacunarity);
		}
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
				W.U32(Operator.Id.A);
				W.U32(Operator.Id.B);
				W.U32(Operator.Id.C);
				W.U32(Operator.Id.D);
				W.U32(uint32(Operator.Version));
				W.U8(uint8(Operator.Stage));
				W.U32(Operator.Inputs.Num());
				for (EVoxelLandGraphValueType Type : Operator.Inputs)
				{
					W.U8(uint8(Type));
				}
				W.U8(uint8(Operator.Output));
				W.U32(uint32(Operator.SeedSlot));
				W.U32(Operator.Configuration.Num());
				for (uint8 Byte : Operator.Configuration)
				{
					W.U8(Byte);
				}
			}
		}
		if (bMetered && IsCancelled())
		{
			return false;
		}
		OutFingerprint = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
		return !(bMetered && IsCancelled());
	}

	static bool SourceValid(
		const FVoxelLandProgramDraft& D, const FVoxelLandValueSource& Source, EVoxelLandParameterType Type)
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

	static bool DraftWithinLimits(
		const FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled, bool bMetered, bool& bCancelled)
	{
		if (D.Instructions.Num() > 8192 || D.Constants.Num() > 65536 || D.Parameters.Num() > 4096
			|| D.Seeds.Num() > 4096 || D.Noise.Num() > 4096 || D.ExternalOperators.Num() > 4096)
		{
			return false;
		}
		int64 TotalConfiguration = 0;
		for (const FVoxelLandParameterSlot& Parameter : D.Parameters)
		{
			if (bMetered && IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			if (Parameter.Name.Len() > 256)
			{
				return false;
			}
		}
		for (const FVoxelLandExternalOperator& Operator : D.ExternalOperators)
		{
			if (bMetered && IsCancelled())
			{
				bCancelled = true;
				return false;
			}
			TotalConfiguration += Operator.Configuration.Num();
			if (Operator.Configuration.Num() > 65536 || TotalConfiguration > 1048576 || Operator.Inputs.Num() > 3)
			{
				return false;
			}
		}
		return true;
	}

	static FVoxelLandError ValidateProgramInputs(
		FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled, bool bMetered)
	{
		for (float V : D.Constants)
		{
			if (bMetered && IsCancelled())
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
			}
			if (!FMath::IsFinite(V))
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Non-finite constant"));
			}
		}
		TSet<FString> Names;
		for (FVoxelLandParameterSlot& P : D.Parameters)
		{
			if (bMetered && IsCancelled())
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
			}
			FString Name;
			if (!NormalizeName(P.Name, Name) || Names.Contains(Name)
				|| (P.Type != EVoxelLandParameterType::Float && P.Type != EVoxelLandParameterType::Int32
					&& P.Type != EVoxelLandParameterType::Bool)
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
			if (!SourceValid(D, Enabled, EVoxelLandParameterType::Bool)
				|| !SourceValid(D, Strength, EVoxelLandParameterType::Float))
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid smooth filter source"));
			}
			const float DefaultStrength =
				Strength.bParameter ? D.Parameters[Strength.Index].FloatDefault : D.Constants[Strength.Index];
			if (DefaultStrength < 0.f || DefaultStrength > 1.f)
			{
				return MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("Smooth strength must be in [0,1]"));
			}
		}
		for (int32 SeedIndex = 0; SeedIndex < D.Seeds.Num(); ++SeedIndex)
		{
			if (bMetered && IsCancelled())
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
			}
			const FVoxelLandSeedSlot& Seed = D.Seeds[SeedIndex];
			if (!Seed.NodeId.IsValid())
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid seed GUID"));
			}
			for (int32 PreviousSeedIndex = 0; PreviousSeedIndex < SeedIndex; ++PreviousSeedIndex)
			{
				if (bMetered && IsCancelled())
				{
					return MakeVoxelLandError(EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
				}
				if (D.Seeds[PreviousSeedIndex].NodeId == Seed.NodeId && D.Seeds[PreviousSeedIndex].Salt == Seed.Salt)
				{
					return MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Duplicate seed GUID and salt"));
				}
			}
		}
		for (const FVoxelLandNoiseConfig& N : D.Noise)
		{
			if (bMetered && IsCancelled())
			{
				return MakeVoxelLandError(EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
			}
			if (N.SeedSlot >= uint32(D.Seeds.Num()) || !SourceValid(D, N.Frequency, EVoxelLandParameterType::Float)
				|| !SourceValid(D, N.Octaves, EVoxelLandParameterType::Int32)
				|| !SourceValid(D, N.Gain, EVoxelLandParameterType::Float)
				|| !SourceValid(D, N.Lacunarity, EVoxelLandParameterType::Float))
			{
				return MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid noise configuration source"));
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

	static bool CopyDraft(const FVoxelLandProgramDraft& Draft, TFunctionRef<bool()> IsCancelled, bool bMetered,
		FVoxelLandProgramDraft& Target)
	{
		if (!bMetered)
		{
			Target = Draft;
			return true;
		}

		// Preserve array order and cancellation polls while taking the immutable snapshot.
		Target.FormatVersion = Draft.FormatVersion;
		Target.RegisterCount = Draft.RegisterCount;
		Target.ExternalFilterIndex = Draft.ExternalFilterIndex;
		Target.SmoothFilter = Draft.SmoothFilter;
		Target.Instructions.Reserve(Draft.Instructions.Num());
		Target.Constants.Reserve(Draft.Constants.Num());
		Target.Parameters.Reserve(Draft.Parameters.Num());
		Target.Seeds.Reserve(Draft.Seeds.Num());
		Target.Noise.Reserve(Draft.Noise.Num());
		return CopyProgramArray(Draft.Instructions, IsCancelled, Target.Instructions)
			&& CopyProgramArray(Draft.Constants, IsCancelled, Target.Constants)
			&& CopyProgramArray(Draft.Parameters, IsCancelled, Target.Parameters)
			&& CopyProgramArray(Draft.Seeds, IsCancelled, Target.Seeds)
			&& CopyProgramArray(Draft.Noise, IsCancelled, Target.Noise);
	}

	static FVoxelLandError BindExternalOperators(const FVoxelLandProgramDraft& D,
		const FVoxelLandOperatorRegistrySnapshot& Providers,
		TArray<TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe>>& OutOperators)
	{
		if (D.ExternalOperators.Num() > 4096
			|| (D.ExternalFilterIndex != INDEX_NONE
				&& (!D.ExternalOperators.IsValidIndex(D.ExternalFilterIndex) || D.SmoothFilter.bPresent)))
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid external operator table"));
		}
		int64 ConfigBytes = 0;
		uint64 WorkerBytes = 0;
		for (int32 OperatorIndex = 0; OperatorIndex < D.ExternalOperators.Num(); ++OperatorIndex)
		{
			const FVoxelLandExternalOperator& Operator = D.ExternalOperators[OperatorIndex];
			if ((OperatorIndex == D.ExternalFilterIndex) != (Operator.Stage == EVoxelLandOperatorStage::OneCellFilter)
				|| (Operator.Stage != EVoxelLandOperatorStage::Sample
					&& Operator.Stage != EVoxelLandOperatorStage::OneCellFilter))
			{
				return FVoxelLandProgramInternals::MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid external operator stage"));
			}
			ConfigBytes += Operator.Configuration.Num();
			if (Operator.Configuration.Num() > 65536 || ConfigBytes > 1048576 || Operator.SeedSlot < 0
				|| Operator.SeedSlot >= D.Seeds.Num() || Operator.Inputs.Num() < 1 || Operator.Inputs.Num() > 3)
			{
				return FVoxelLandProgramInternals::MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("External operator exceeds limits"));
			}
			TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
				Providers.Resolve(Operator.Id, Operator.Version);
			FString Error;
			if (!Provider || Provider->Stage() != Operator.Stage || Provider->InputTypes() != Operator.Inputs
				|| Provider->OutputType() != Operator.Output || Provider->MaximumWorkerBytes() > 1048576
				|| !Provider->ValidateConfiguration(Operator.Configuration, Error))
			{
				return FVoxelLandProgramInternals::MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram,
					TEXT("Missing, incompatible or invalid external operator provider"));
			}
			WorkerBytes += uint64(Provider->MaximumWorkerBytes())
						 * (Provider->SimdRequirement() == EVoxelLandOperatorSimdRequirement::None ? 1u : 2u);
			if (WorkerBytes > 8u * 1024u * 1024u)
			{
				return FVoxelLandProgramInternals::MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("External operator worker budget exceeded"));
			}
			OutOperators.Add(MoveTemp(Provider));
		}
		return {};
	}

	static FVoxelLandError ValidateInstructions(
		const FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled, bool bMetered)
	{
		TArray<bool> Initialized;
		Initialized.Init(false, D.RegisterCount);
		TArray<uint8> RegisterTypes;
		RegisterTypes.Init(0, D.RegisterCount);
		int32 Stores = 0;
		for (int32 Index = 0; Index < D.Instructions.Num(); ++Index)
		{
			if (bMetered && IsCancelled())
			{
				return FVoxelLandProgramInternals::MakeVoxelLandError(
					EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
			}
			if (FVoxelLandError Error = ValidateInstruction(D, Index, Initialized, RegisterTypes, Stores))
			{
				return Error;
			}
		}
		if (Stores != 1)
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("Exactly one density store is required"));
		}
		return {};
	}

private:
	template <typename ElementType>
	static bool CopyProgramArray(
		const TArray<ElementType>& Source, TFunctionRef<bool()> IsCancelled, TArray<ElementType>& Target)
	{
		for (const ElementType& Value : Source)
		{
			if (IsCancelled())
			{
				return false;
			}
			Target.Add(Value);
		}
		return true;
	}

	static FVoxelLandError ValidatePrimitiveConstants(
		const FVoxelLandProgramDraft& D, const FVoxelLandInstruction& I, int32 Index)
	{
		switch (I.Op)
		{
			case EVoxelLandOpcode::Plane:
				if (uint64(I.Imm) + 4 > uint64(D.Constants.Num()))
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Plane constants out of range"), Index);
				}
				{
					const double X = D.Constants[I.Imm], Y = D.Constants[I.Imm + 1], Z = D.Constants[I.Imm + 2];
					if (FMath::Abs(X * X + Y * Y + Z * Z - 1.0) > 0.00001)
					{
						return FVoxelLandProgramInternals::MakeVoxelLandError(
							EVoxelLandErrorCode::InvalidProgram, TEXT("Plane normal must be unit length"), Index);
					}
				}
				break;
			case EVoxelLandOpcode::Sphere:
				if (I.Imm >= uint32(D.Constants.Num()) || D.Constants[I.Imm] <= 0)
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid sphere radius"), Index);
				}
				break;
			case EVoxelLandOpcode::Box:
				if (uint64(I.Imm) + 3 > uint64(D.Constants.Num()) || D.Constants[I.Imm] <= 0
					|| D.Constants[I.Imm + 1] <= 0 || D.Constants[I.Imm + 2] <= 0)
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid box extents"), Index);
				}
				break;
			default:
				checkNoEntry();
				break;
		}
		return {};
	}

	static FVoxelLandError ResolveExternalInstruction(const FVoxelLandProgramDraft& D, const FVoxelLandInstruction& I,
		int32 Index, const FVoxelLandExternalOperator*& OutOperator)
	{
		if (I.Imm >= uint32(D.ExternalOperators.Num()))
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("External operator index out of range"), Index);
		}
		const FVoxelLandExternalOperator& Operator = D.ExternalOperators[I.Imm];
		if (Operator.Stage != EVoxelLandOperatorStage::Sample)
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("Filter cannot execute as a sample operator"), Index);
		}
		if (I.Dst == I.A || (Operator.Inputs.Num() > 1 && I.Dst == I.B) || (Operator.Inputs.Num() > 2 && I.Dst == I.C))
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("External output must not alias an input"), Index);
		}
		OutOperator = &Operator;
		return {};
	}

	static FVoxelLandError ValidateInstruction(const FVoxelLandProgramDraft& D, int32 Index, TArray<bool>& Initialized,
		TArray<uint8>& RegisterTypes, int32& Stores)
	{
		const FVoxelLandInstruction& I = D.Instructions[Index];
		bool A = false, B = false, C = false, ExtraD = false, ExtraE = false, ExtraF = false, ImmReg = false,
			 Writes = true;
		uint8 TypeA = 1, TypeB = 1, TypeC = 1, WriteType = 1;
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
			case EVoxelLandOpcode::LoadY:
			case EVoxelLandOpcode::LoadZ:
				break;
			case EVoxelLandOpcode::LoadConstant:
				if (I.Imm >= uint32(D.Constants.Num()))
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Constant out of range"), Index);
				}
				break;
			case EVoxelLandOpcode::LoadFloatParameter:
				if (I.Imm >= uint32(D.Parameters.Num()) || D.Parameters[I.Imm].Type != EVoxelLandParameterType::Float)
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
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
			case EVoxelLandOpcode::Sphere:
			case EVoxelLandOpcode::Box:
				A = B = C = true;
				if (FVoxelLandError Error = ValidatePrimitiveConstants(D, I, Index))
				{
					return Error;
				}
				break;
			case EVoxelLandOpcode::Perlin3:
			case EVoxelLandOpcode::Fbm3:
			case EVoxelLandOpcode::Value3:
			case EVoxelLandOpcode::Simplex3:
				C = true;
				[[fallthrough]];
			case EVoxelLandOpcode::Perlin2:
			case EVoxelLandOpcode::Fbm2:
			case EVoxelLandOpcode::Value2:
			case EVoxelLandOpcode::Simplex2:
				A = B = true;
				if (I.Imm >= uint32(D.Noise.Num()))
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Noise config out of range"), Index);
				}
				break;
			case EVoxelLandOpcode::LoadIntConstant:
				WriteType = 2;
				break;
			case EVoxelLandOpcode::LoadBoolConstant:
				if (I.Imm > 1)
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Boolean constant must be 0 or 1"), Index);
				}
				WriteType = 3;
				break;
			case EVoxelLandOpcode::LoadIntParameter:
			case EVoxelLandOpcode::LoadBoolParameter:
			{
				const bool bBool = I.Op == EVoxelLandOpcode::LoadBoolParameter;
				if (I.Imm >= uint32(D.Parameters.Num())
					|| D.Parameters[I.Imm].Type
						   != (bBool ? EVoxelLandParameterType::Bool : EVoxelLandParameterType::Int32))
				{
					return FVoxelLandProgramInternals::MakeVoxelLandError(
						EVoxelLandErrorCode::InvalidProgram, TEXT("Invalid typed parameter"), Index);
				}
				WriteType = bBool ? 3 : 2;
				break;
			}
			case EVoxelLandOpcode::IntToFloat:
				A = true;
				TypeA = 2;
				break;
			case EVoxelLandOpcode::Less:
			case EVoxelLandOpcode::LessEqual:
			case EVoxelLandOpcode::Equal:
				A = B = true;
				WriteType = 3;
				break;
			case EVoxelLandOpcode::BoolAnd:
			case EVoxelLandOpcode::BoolOr:
				A = B = true;
				TypeA = TypeB = WriteType = 3;
				break;
			case EVoxelLandOpcode::BoolNot:
				A = true;
				TypeA = WriteType = 3;
				break;
			case EVoxelLandOpcode::SelectFloat:
				A = B = C = true;
				TypeA = 3;
				break;
			case EVoxelLandOpcode::SelectInt:
				A = B = C = true;
				TypeA = 3;
				TypeB = TypeC = WriteType = 2;
				break;
			case EVoxelLandOpcode::SelectBool:
				A = B = C = true;
				TypeA = TypeB = TypeC = WriteType = 3;
				break;
			case EVoxelLandOpcode::Power:
				A = B = true;
				break;
			case EVoxelLandOpcode::Clamp:
				A = B = C = true;
				break;
			case EVoxelLandOpcode::DynamicPlane:
				A = B = C = ExtraD = ExtraE = ExtraF = ImmReg = true;
				break;
			case EVoxelLandOpcode::DynamicSphere:
				A = B = C = ExtraD = true;
				break;
			case EVoxelLandOpcode::DynamicBox:
				A = B = C = ExtraD = ExtraE = ExtraF = true;
				break;
			case EVoxelLandOpcode::ExternalOperator:
			{
				const FVoxelLandExternalOperator* Operator = nullptr;
				if (FVoxelLandError Error = ResolveExternalInstruction(D, I, Index, Operator))
				{
					return Error;
				}
				A = true;
				B = Operator->Inputs.Num() > 1;
				C = Operator->Inputs.Num() > 2;
				TypeA = uint8(Operator->Inputs[0]) + 1;
				if (B)
				{
					TypeB = uint8(Operator->Inputs[1]) + 1;
				}
				if (C)
				{
					TypeC = uint8(Operator->Inputs[2]) + 1;
				}
				WriteType = uint8(Operator->Output) + 1;
				break;
			}
			case EVoxelLandOpcode::StoreDensity:
				A = true;
				Writes = false;
				++Stores;
				break;
			default:
				return FVoxelLandProgramInternals::MakeVoxelLandError(
					EVoxelLandErrorCode::InvalidProgram, TEXT("Unknown opcode"), Index);
		}
		if ((A && (I.A >= D.RegisterCount || !Initialized[I.A])) || (B && (I.B >= D.RegisterCount || !Initialized[I.B]))
			|| (C && (I.C >= D.RegisterCount || !Initialized[I.C]))
			|| (ExtraD && (I.D >= D.RegisterCount || !Initialized[I.D]))
			|| (ExtraE && (I.E >= D.RegisterCount || !Initialized[I.E]))
			|| (ExtraF && (I.F >= D.RegisterCount || !Initialized[I.F]))
			|| (ImmReg && (I.Imm >= uint32(D.RegisterCount) || !Initialized[I.Imm]))
			|| (Writes && I.Dst >= D.RegisterCount))
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(EVoxelLandErrorCode::InvalidProgram,
				TEXT("Register out of range or read before initialization"), Index);
		}
		if ((A && RegisterTypes[I.A] != TypeA) || (B && RegisterTypes[I.B] != TypeB)
			|| (C && RegisterTypes[I.C] != TypeC) || (ExtraD && RegisterTypes[I.D] != 1)
			|| (ExtraE && RegisterTypes[I.E] != 1) || (ExtraF && RegisterTypes[I.F] != 1)
			|| (ImmReg && RegisterTypes[I.Imm] != 1))
		{
			return FVoxelLandProgramInternals::MakeVoxelLandError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("Register type mismatch"), Index);
		}
		if (Writes)
		{
			RegisterTypes[I.Dst] = WriteType;
			Initialized[I.Dst] = true;
		}
		return {};
	}
};

FVoxelLandError FVoxelLandProgram::Validate(
	const FVoxelLandProgramDraft& Draft, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	return Validate(Draft, Providers, OutProgram);
}

FVoxelLandError FVoxelLandProgram::Validate(const FVoxelLandProgramDraft& Draft,
	const FVoxelLandOperatorRegistrySnapshot& Providers,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	return ValidateInternal(Draft, Providers, [] { return false; }, false, nullptr, OutProgram);
}

FVoxelLandM5Diagnostic FVoxelLandProgram::ValidateMetered(const FVoxelLandProgramDraft& Draft,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	auto Failure = [](EVoxelLandM5ErrorCode Code, const TCHAR* Message) {
		FVoxelLandM5Diagnostic Error;
		Error.Code = Code;
		Error.Stage = TEXT("M5.ProgramValidation");
		Error.Message = Message;
		return Error;
	};
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	if (IsInGameThread())
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Program validation requires a worker thread"));
	}
	// Provider virtual methods, registry enumeration and module leases are not
	// metered yet. Do not inspect or invoke them on this path.
	if (!Draft.ExternalOperators.IsEmpty() || Draft.ExternalFilterIndex != INDEX_NONE)
	{
		return Failure(EVoxelLandM5ErrorCode::ProviderUnavailable, TEXT("Metered external operators are unsupported"));
	}
	const int64 Instructions = Draft.Instructions.Num();
	const int64 Constants = Draft.Constants.Num();
	const int64 Parameters = Draft.Parameters.Num();
	const int64 Seeds = Draft.Seeds.Num();
	const int64 Noise = Draft.Noise.Num();
	if (Instructions > 8192 || Constants > 65536 || Parameters > 4096 || Seeds > 4096 || Noise > 4096
		|| Draft.FormatVersion != FVoxelLandProgramDraft::CurrentFormatVersion || Draft.RegisterCount < 1
		|| Draft.RegisterCount > 1024)
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Program exceeds input limits"));
	}
	// Counts are capped above, so every term fits int64. Worst case includes
	// quadratic parameter-name comparisons (up to 256 chars) and seed checks,
	// per-item polling, normalization, fingerprint serialization and hashing.
	const int64 Work = 100000 + (Parameters + 1) * (Parameters + 1) * 512 + (Seeds + 1) * (Seeds + 1) * 4
					 + Instructions * 64 + Constants * 16 + Noise * 128 + Parameters * 512;
	if (!Budget->TryChargeWork(Work))
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program validation work budget exceeded"));
	}
	// Charge a conservative logical allocation allowance for a draft copy,
	// normalized names, set/register scratch and canonical fingerprint bytes.
	// Caller-owned inputs and allocator/control-block overhead are excluded.
	const int64 Payload = Instructions * int64(sizeof(FVoxelLandInstruction)) + Constants * int64(sizeof(float))
						+ Parameters * (int64(sizeof(FVoxelLandParameterSlot)) + 256 * int64(sizeof(TCHAR)))
						+ Seeds * int64(sizeof(FVoxelLandSeedSlot)) + Noise * int64(sizeof(FVoxelLandNoiseConfig));
	const int64 ReservedBytes = 65536 + 8 * Payload;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ReservedBytes, Lease))
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Program validation memory budget exceeded"));
	}
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	FVoxelLandOperatorRegistrySnapshot NoProviders;
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Result;
	const FVoxelLandError Error = ValidateInternal(Draft, NoProviders, IsCancelled, true, MoveTemp(Lease), Result);
	if (Error)
	{
		FVoxelLandM5Diagnostic ResultError;
		ResultError.Code = Error.Code == EVoxelLandErrorCode::Cancelled ? EVoxelLandM5ErrorCode::Cancelled
																		: EVoxelLandM5ErrorCode::InvalidInput;
		ResultError.Stage = TEXT("M5.ProgramValidation");
		ResultError.Message = Error.Message;
		return ResultError;
	}
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	OutProgram = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandProgram::ValidateInternal(const FVoxelLandProgramDraft& Draft,
	const FVoxelLandOperatorRegistrySnapshot& Providers, TFunctionRef<bool()> IsCancelled, bool bMetered,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	OutProgram.Reset();
	if (bMetered && IsCancelled())
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Program validation requires a worker thread"));
	}
	bool bCancelled = false;
	if (!FVoxelLandProgramInternals::DraftWithinLimits(Draft, IsCancelled, bMetered, bCancelled))
	{
		return bCancelled ? FVoxelLandProgramInternals::MakeVoxelLandError(
								EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"))
						  : FVoxelLandProgramInternals::MakeVoxelLandError(
								EVoxelLandErrorCode::InvalidProgram, TEXT("Program exceeds bounded input limits"));
	}
	if (Draft.FormatVersion != FVoxelLandProgramDraft::CurrentFormatVersion || Draft.RegisterCount < 1
		|| Draft.RegisterCount > 1024)
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported version or register count"));
	}
	TSharedPtr<FVoxelLandProgram, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandProgram);
	Result->M5Lease = MoveTemp(Lease);
	if (!FVoxelLandProgramInternals::CopyDraft(Draft, IsCancelled, bMetered, Result->Data))
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	if (FVoxelLandError Error = FVoxelLandProgramInternals::ValidateProgramInputs(Result->Data, IsCancelled, bMetered))
	{
		return Error;
	}
	const FVoxelLandProgramDraft& D = Result->Data;
	if (FVoxelLandError Error = FVoxelLandProgramInternals::BindExternalOperators(D, Providers, Result->Operators))
	{
		return Error;
	}
	if (FVoxelLandError Error = FVoxelLandProgramInternals::ValidateInstructions(D, IsCancelled, bMetered))
	{
		return Error;
	}
	if (!FVoxelLandProgramInternals::ComputeProgramFingerprint(D, IsCancelled, bMetered, Result->Fingerprint))
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::Cancelled, TEXT("Program validation cancelled"));
	}
	OutProgram = Result;
	return {};
}

namespace VoxelLandDeterminism
{
class FVoxelLandSeedMixer final
{
public:
	static uint64 Mix64(uint64 X)
	{
		X += WeylIncrement;
		X = (X ^ (X >> 30)) * FirstMultiplier;
		X = (X ^ (X >> 27)) * SecondMultiplier;
		return X ^ (X >> 31);
	}

private:
	static constexpr uint64 WeylIncrement = 0x9E3779B97F4A7C15ull;
	static constexpr uint64 FirstMultiplier = 0xBF58476D1CE4E5B9ull;
	static constexpr uint64 SecondMultiplier = 0x94D049BB133111EBull;
};

uint64 DeriveNodeSeed(uint64 WorldSeed, const FGuid& G, uint32 Salt)
{
	const uint64 H =
		FVoxelLandSeedMixer::Mix64((uint64(G.A) << 32 | G.B) ^ FVoxelLandSeedMixer::Mix64(uint64(G.C) << 32 | G.D));
	return FVoxelLandSeedMixer::Mix64(WorldSeed ^ FVoxelLandSeedMixer::Mix64(H + Salt));
}

FVoxelLandError Quantize(float DistanceCm, float VoxelSizeCm, int16& OutDensity)
{
	if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(VoxelSizeCm) || VoxelSizeCm <= 0)
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::NonFiniteDensity, TEXT("Invalid density or voxel size"));
	}
	const double Scaled = (double(DistanceCm) / double(VoxelSizeCm)) * 1024.0;
	if (!FMath::IsFinite(Scaled))
	{
		return FVoxelLandProgramInternals::MakeVoxelLandError(
			EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite scaled density"));
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
