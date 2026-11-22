#include "World/VoxelLandM4PrepassIdentity.h"
#include "World/VoxelLandM4PrepassKeyInputs.h"
#include "World/VoxelLandM4OverrideValidation.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4Domain.h"
#include "Generation/VoxelLandM4ErosionRandom.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Graph/VoxelLandM5ProgramLoader.h"
#include "World/VoxelLandM4CurveValidation.h"
#include "World/VoxelLandM5MeteredPrepass.h"
#include "World/VoxelLandM5PrepassBytes.h"

namespace VoxelLandM4PrepassIdentity
{
class FVoxelLandM4PrepassKeyWriter final
{
public:
	static constexpr int32 MaxHeightQ8 = 512000000;
	static constexpr int32 MaxDropletCount = 65536;
	static constexpr int32 MaxErosionSteps = 128;
	static constexpr int32 MaxThermalIterations = 32;
	static constexpr uint32 LegacyEncodingVersion = 0;
	static constexpr uint32 CurrentEncodingVersion = 1;
	static constexpr uint32 HeightPrepassAlgorithmVersion = 1;

	static FVoxelLandError Build(const FVoxelLandM4WorldSnapshot& Snapshot, const FVoxelLandM4PrepassKeyInputs& Inputs,
		TArray<uint8>& OutBytes, TFunctionRef<bool()> IsCancelled);
	static FVoxelLandError M4IdentityFailure(const TCHAR* Message);
	static void M4IdentityU64(FCanonicalWriter& W, uint64 Value);
	static void M4IdentityGuid(FCanonicalWriter& W, const FGuid& Id);
	static void M4IdentityFloat(FCanonicalWriter& W, float Value);
	static bool M4IdentityProgram(
		FCanonicalWriter& W, const FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled);
	static bool M4IdentityChannel(FCanonicalWriter& W, const FVoxelLandM4ControlSnapshot& Channel,
		const FVoxelLandProgram& Program, bool bPrevalidated, TFunctionRef<bool()> IsCancelled);
};

FVoxelLandError FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidParameter;
	Error.Message = Message;
	return Error;
}

void FVoxelLandM4PrepassKeyWriter::M4IdentityU64(FCanonicalWriter& W, uint64 Value)
{
	W.U32(uint32(Value));
	W.U32(uint32(Value >> 32));
}

void FVoxelLandM4PrepassKeyWriter::M4IdentityGuid(FCanonicalWriter& W, const FGuid& Id)
{
	W.U32(Id.A);
	W.U32(Id.B);
	W.U32(Id.C);
	W.U32(Id.D);
}

void FVoxelLandM4PrepassKeyWriter::M4IdentityFloat(FCanonicalWriter& W, float Value)
{
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	W.U32(Bits);
}

bool FVoxelLandM4PrepassKeyWriter::M4IdentityProgram(
	FCanonicalWriter& W, const FVoxelLandProgramDraft& D, TFunctionRef<bool()> IsCancelled)
{
	W.U32(D.FormatVersion);
	W.U32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	W.U32(uint32(D.RegisterCount));
	W.U32(uint32(D.Instructions.Num()));
	for (const FVoxelLandInstruction& I : D.Instructions)
	{
		if (IsCancelled())
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
	W.U32(uint32(D.Constants.Num()));
	for (float Value : D.Constants)
	{
		if (IsCancelled())
		{
			return false;
		}
		M4IdentityFloat(W, Value);
	}
	W.U32(uint32(D.Parameters.Num()));
	for (const FVoxelLandParameterSlot& P : D.Parameters)
	{
		if (IsCancelled())
		{
			return false;
		}
		W.String(P.Name);
		W.U8(uint8(P.Type));
		M4IdentityFloat(W, P.FloatDefault);
		W.U32(uint32(P.IntDefault));
	}
	W.U32(uint32(D.Seeds.Num()));
	for (const FVoxelLandSeedSlot& S : D.Seeds)
	{
		if (IsCancelled())
		{
			return false;
		}
		M4IdentityGuid(W, S.NodeId);
		W.U32(S.Salt);
	}
	W.U32(uint32(D.Noise.Num()));
	for (const FVoxelLandNoiseConfig& N : D.Noise)
	{
		if (IsCancelled())
		{
			return false;
		}
		W.U32(N.SeedSlot);
		W.Source(N.Frequency);
		W.Source(N.Octaves);
		W.Source(N.Gain);
		W.Source(N.Lacunarity);
	}
	return !IsCancelled();
}

bool FVoxelLandM4PrepassKeyWriter::M4IdentityChannel(FCanonicalWriter& W, const FVoxelLandM4ControlSnapshot& Channel,
	const FVoxelLandProgram& Program, bool bPrevalidated, TFunctionRef<bool()> IsCancelled)
{
	FString OverrideError;
	if (IsCancelled() || (!bPrevalidated && !VoxelLandM4OverrideValidation::Validate(Channel, OverrideError))
		|| VoxelLandM4ControlGrid::ValidateProgram(Program) || Channel.CookedCurve.BakeVersion != 1
		|| Channel.CookedCurve.SamplesQ8.Num() != 4097 || Channel.Payload.Parameters.Num() > 128
		|| Channel.Overrides.Num() > 128)
	{
		return false;
	}
	if (!M4IdentityProgram(W, Program.GetData(), IsCancelled))
	{
		return false;
	}
	// Parameter IDs map authored overrides to the VM names used by the generator.
	W.U32(uint32(Channel.Payload.Parameters.Num()));
	for (const FVoxelLandSerializedParameter& P : Channel.Payload.Parameters)
	{
		if (IsCancelled())
		{
			return false;
		}
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
	TArray<const FVoxelLandParameterOverride*> Overrides;
	Overrides.Reserve(Channel.Overrides.Num());
	for (const FVoxelLandParameterOverride& O : Channel.Overrides)
	{
		if (IsCancelled())
		{
			return false;
		}
		// Only reachable compiled parameters can affect the prepass.
		if (Channel.Payload.Parameters.ContainsByPredicate(
				[&O](const FVoxelLandSerializedParameter& P) { return P.Id == O.ParameterId; }))
		{
			Overrides.Add(&O);
		}
	}
	W.U32(uint32(Overrides.Num()));
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
	for (const FVoxelLandParameterOverride* Entry : Overrides)
	{
		if (IsCancelled())
		{
			return false;
		}
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
		if (IsCancelled())
		{
			return false;
		}
		if (Sample < -MaxHeightQ8 || Sample > MaxHeightQ8)
		{
			return false;
		}
		W.U32(uint32(Sample));
	}
	return true;
}

FVoxelLandError FVoxelLandM4PrepassKeyWriter::Build(const FVoxelLandM4WorldSnapshot& Snapshot,
	const FVoxelLandM4PrepassKeyInputs& Inputs, TArray<uint8>& OutBytes, TFunctionRef<bool()> IsCancelled)
{
	const FVoxelLandM4DomainSettings& Domain = Snapshot.Domain;
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(TEXT("M4 prepass identity cancelled"));
	}
	FString StageError;
	if (!VoxelLandM4HeightStageValidation::Validate(Snapshot, StageError))
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(*StageError);
	}
	if ((Inputs.EncodingVersion != LegacyEncodingVersion && Inputs.EncodingVersion != CurrentEncodingVersion)
		|| (Domain.Mode != EVoxelLandM4DomainMode::Finite
			&& !(Inputs.bAllowsUnlimited && Domain.Mode == EVoxelLandM4DomainMode::Unlimited))
		|| VoxelLandM4Domain::Validate(Domain) || Snapshot.FormatVersion != 1 || Snapshot.BaseHeightQ8 < -MaxHeightQ8
		|| Snapshot.BaseHeightQ8 > MaxHeightQ8)
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(TEXT("Invalid finite M4 prepass identity inputs"));
	}
	if (Domain.bGlobalErosion
		&& (!Snapshot.ErosionStageId.IsValid() || Snapshot.ErosionSalt < 0 || Snapshot.DropletCount < 0
			|| Snapshot.DropletCount > MaxDropletCount || Snapshot.Erosion.MaxSteps < 0
			|| Snapshot.Erosion.MaxSteps > MaxErosionSteps || Snapshot.Erosion.ThermalIterations < 0
			|| Snapshot.Erosion.ThermalIterations > MaxThermalIterations || Snapshot.Erosion.CapacityMaxQ8 < 0
			|| Snapshot.Erosion.CapacityMaxQ8 > MaxHeightQ8 || Snapshot.Erosion.TalusQ8 < 0
			|| Snapshot.Erosion.TalusQ8 > MaxHeightQ8 || Snapshot.Erosion.MaxTransferQ8 < 0
			|| Snapshot.Erosion.MaxTransferQ8 > MaxHeightQ8))
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(TEXT("Invalid M4 erosion identity inputs"));
	}
	FCanonicalWriter W;
	if (Inputs.ReserveBytes > 0)
	{
		W.Bytes.Reserve(Inputs.ReserveBytes);
	}
	W.U8('V');
	W.U8('L');
	W.U8('P');
	W.U8('C');
	W.U32(CurrentEncodingVersion);
	W.U32(HeightPrepassAlgorithmVersion);
	W.U32(uint32(Snapshot.HeightStage.Version));
	W.U8(uint8(Domain.Mode));
	FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Snapshot.Seed));
	if (Domain.Mode == EVoxelLandM4DomainMode::Finite)
	{
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Domain.OriginXCm));
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Domain.OriginYCm));
		W.U32(uint32(Domain.CellsX));
		W.U32(uint32(Domain.CellsY));
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Domain.GridSpacingCm));
	}
	W.U32(uint32(Snapshot.BaseHeightQ8));
	W.U8(Domain.bGlobalErosion ? 1 : 0);
	if (Domain.bGlobalErosion)
	{
		W.U32(VoxelLandM4ErosionRandom::AlgorithmVersion);
		FVoxelLandM4PrepassKeyWriter::M4IdentityGuid(W, Snapshot.ErosionStageId);
		W.U32(uint32(Snapshot.ErosionSalt));
		W.U32(uint32(Snapshot.DropletCount));
		W.U32(uint32(Snapshot.Erosion.MaxSteps));
		W.U32(uint32(Snapshot.Erosion.ThermalIterations));
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Snapshot.Erosion.CapacityMaxQ8));
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Snapshot.Erosion.TalusQ8));
		FVoxelLandM4PrepassKeyWriter::M4IdentityU64(W, uint64(Snapshot.Erosion.MaxTransferQ8));
	}
	if (!M4IdentityChannel(W, Snapshot.Continental, Inputs.Continental, Inputs.bIsPrevalidated, IsCancelled)
		|| !M4IdentityChannel(W, Snapshot.Mountain, Inputs.Mountain, Inputs.bIsPrevalidated, IsCancelled)
		|| !M4IdentityChannel(W, Snapshot.Peaks, Inputs.Peaks, Inputs.bIsPrevalidated, IsCancelled))
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(
			TEXT("Unsupported or invalid M4 control identity inputs"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassKeyWriter::M4IdentityFailure(TEXT("M4 prepass identity cancelled"));
	}
	OutBytes = MoveTemp(W.Bytes);
	return {};
}

FVoxelLandError BuildCandidate(const FVoxelLandM4WorldSnapshot& Snapshot, const FVoxelLandProgram& Continental,
	const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks, TArray<uint8>& OutBytes, uint32 EncodingVersion)
{
	auto NeverCancel = [] {
		return false;
	};
	return FVoxelLandM4PrepassKeyWriter::Build(
		Snapshot, {Continental, Mountain, Peaks, EncodingVersion}, OutBytes, NeverCancel);
}

FVoxelLandError BuildWorldInputs(const FVoxelLandM4WorldSnapshot& Snapshot, const FVoxelLandProgram& Continental,
	const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks, TArray<uint8>& OutBytes)
{
	auto NeverCancel = [] {
		return false;
	};
	return FVoxelLandM4PrepassKeyWriter::Build(Snapshot,
		{Continental, Mountain, Peaks, FVoxelLandM4PrepassKeyWriter::CurrentEncodingVersion, true}, OutBytes,
		NeverCancel);
}
}

class FVoxelLandM4PrepassOverrideChecks final
{
public:
	static FVoxelLandM5Diagnostic M5PrepassFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.PrepassIdentity");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5MeteredPrepass::Build(const FVoxelLandM4WorldSnapshot& Snapshot,
	uint32 EncodingVersion, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe>& OutBytes)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Prepass identity requires a worker thread"));
	}
	// Version 1 projection validation traverses reflected FText and nested graph
	// state. Do not pretend that work is metered until its bounded replacement exists.
	if (Snapshot.HeightStage.Version != 0)
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Metered height-stage projection is unavailable"));
	}
	if (EncodingVersion != 0 && EncodingVersion != 1)
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::UnsupportedVersion, TEXT("Unsupported prepass identity version"));
	}
	if (Snapshot.Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Infinite worlds have no finite global prepass"));
	}
	if (VoxelLandM4Domain::Validate(Snapshot.Domain))
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid M4 domain"));
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Programs[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (IsCancelled())
		{
			return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (FVoxelLandM5Diagnostic D =
				VoxelLandM4OverrideValidation::ValidateMetered(*Channels[Index], Budget, IsCancelled);
			D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
		if (FVoxelLandM5Diagnostic D =
				VoxelLandM4CurveValidation::ValidateMetered(*Channels[Index], Budget, IsCancelled);
			D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
		if (FVoxelLandM5Diagnostic D = FVoxelLandM5ProgramLoader::Load(
				Channels[Index]->Source, Channels[Index]->Payload, Budget, IsCancelled, Programs[Index]);
			D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
	}
	// Byte upper bound follows the shared writer's fixed-width fields and the
	// loader/override limits. ASCII names are bounded by M4 override validation.
	int64 ByteUpper = 512;
	int64 Records = 0;
	int64 OverridePairs = 0;
	auto AddProduct = [](int64 Count, int64 Width, int64& Total) {
		int64 Part = 0;
		return TryMultiplyNonNegative(Count, Width, Part) && TryAdd(Total, Part, Total);
	};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const auto& D = Programs[Index]->GetData();
		const auto& C = *Channels[Index];
		if (D.Parameters.Num() > 128 || C.Payload.Parameters.Num() > 128 || C.Overrides.Num() > 128
			|| C.CookedCurve.SamplesQ8.Num() != 4097 || !AddProduct(D.Instructions.Num(), 20, ByteUpper)
			|| !AddProduct(D.Constants.Num(), 4, ByteUpper) || !AddProduct(D.Parameters.Num(), 141, ByteUpper)
			|| !AddProduct(D.Seeds.Num(), 20, ByteUpper) || !AddProduct(D.Noise.Num(), 24, ByteUpper)
			|| !AddProduct(C.Payload.Parameters.Num(), 157, ByteUpper) || !AddProduct(C.Overrides.Num(), 21, ByteUpper)
			|| !AddProduct(4097, 4, ByteUpper) || !TryAdd(ByteUpper, 128, ByteUpper)
			|| !TryAdd(Records,
				int64(D.Instructions.Num()) + D.Constants.Num() + D.Parameters.Num() + D.Seeds.Num() + D.Noise.Num()
					+ C.Payload.Parameters.Num() + C.Overrides.Num() + 4097,
				Records)
			|| !AddProduct(C.Overrides.Num(), C.Overrides.Num(), OverridePairs))
		{
			return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Prepass byte bounds exceeded"));
		}
	}
	int64 Work = 0, Part = 0, ScratchBytes = 0;
	if (ByteUpper > 8 * 1024 * 1024 || !TryMultiplyNonNegative(ByteUpper, 8, Work)
		|| !TryMultiplyNonNegative(Records, 8, Part) || !TryAdd(Work, Part, Work)
		|| !TryMultiplyNonNegative(OverridePairs, 8, Part) || !TryAdd(Work, Part, Work) || !TryAdd(Work, 4096, Work)
		|| !TryAdd(ByteUpper, 16384, ScratchBytes) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Prepass work exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ScratchBytes, Lease))
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Prepass memory exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5PrepassBytes, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5PrepassBytes(Budget, Lease));
	bool bCancelled = false;
	auto Poll = [&]() {
		bCancelled |= IsCancelled();
		return bCancelled;
	};
	if (const FVoxelLandError Error = VoxelLandM4PrepassIdentity::FVoxelLandM4PrepassKeyWriter::Build(Snapshot,
			{*Programs[0], *Programs[1], *Programs[2], EncodingVersion, false, true, int32(ByteUpper)}, Result->Bytes,
			Poll))
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			bCancelled ? EVoxelLandM5ErrorCode::Cancelled : EVoxelLandM5ErrorCode::InvalidInput,
			TEXT("Prepass serialization failed or was cancelled"));
	}
	if (Result->Bytes.Num() > ByteUpper)
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Prepass output exceeded reserved bytes"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM4PrepassOverrideChecks::M5PrepassFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	OutBytes = MoveTemp(Result);
	return {};
}
