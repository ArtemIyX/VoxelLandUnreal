#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Compilation/VoxelLandGraphFunctionFlatten.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "Operators/VoxelLandOperator.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Hash/xxhash.h"

class FVoxelLandProgramCodecFormat final
{
public:
	static bool LessGuid(const FGuid& A, const FGuid& B);
	static void WriteGuid(FCanonicalWriter& W, const FGuid& G);
	static int64 SignedBits(uint64 Value);
	static uint64 UnsignedBits(int64 Value);
	static FVoxelLandSerializedValueSource PackSource(const FVoxelLandValueSource& Source);
	static FVoxelLandValueSource LoadSource(const FVoxelLandSerializedValueSource& Source);
};

bool FVoxelLandProgramCodecFormat::LessGuid(const FGuid& A, const FGuid& B)
{
	if (A.A != B.A)
	{
		return A.A < B.A;
	}
	if (A.B != B.B)
	{
		return A.B < B.B;
	}
	if (A.C != B.C)
	{
		return A.C < B.C;
	}
	return A.D < B.D;
}

void FVoxelLandProgramCodecFormat::WriteGuid(FCanonicalWriter& W, const FGuid& G)
{
	W.U32(G.A);
	W.U32(G.B);
	W.U32(G.C);
	W.U32(G.D);
}

int64 FVoxelLandProgramCodecFormat::SignedBits(uint64 Value)
{
	int64 Result;
	FMemory::Memcpy(&Result, &Value, sizeof(Result));
	return Result;
}

uint64 FVoxelLandProgramCodecFormat::UnsignedBits(int64 Value)
{
	uint64 Result;
	FMemory::Memcpy(&Result, &Value, sizeof(Result));
	return Result;
}

FVoxelLandSerializedValueSource FVoxelLandProgramCodecFormat::PackSource(const FVoxelLandValueSource& Source)
{
	FVoxelLandSerializedValueSource Result;
	Result.bParameter = Source.bParameter;
	Result.Index = int32(Source.Index);
	return Result;
}

FVoxelLandValueSource FVoxelLandProgramCodecFormat::LoadSource(const FVoxelLandSerializedValueSource& Source)
{
	FVoxelLandValueSource Result;
	Result.bParameter = Source.bParameter;
	Result.Index = uint32(Source.Index);
	return Result;
}

uint64 FVoxelLandProgramCodec::SourceFingerprint(const FVoxelLandGraphSnapshot& Snapshot)
{
	FCanonicalWriter W;
	W.U32(1);
	W.U32(uint32(Snapshot.SourceVersion));
	TArray<FVoxelLandGraphParameter> Parameters = Snapshot.Parameters;
	Parameters.Sort([](const FVoxelLandGraphParameter& A, const FVoxelLandGraphParameter& B) {
		return FVoxelLandProgramCodecFormat::LessGuid(A.Id, B.Id);
	});
	W.U32(Parameters.Num());
	for (const FVoxelLandGraphParameter& P : Parameters)
	{
		FVoxelLandProgramCodecFormat::WriteGuid(W, P.Id);
		W.String(P.Name.ToLower());
		W.U8(uint8(P.Type));
		W.Float(P.FloatDefault);
		W.U32(uint32(P.IntDefault));
		W.U8(P.BoolDefault ? 1 : 0);
		W.U8(P.bHasRange ? 1 : 0);
		W.Float(P.FloatMin);
		W.Float(P.FloatMax);
		W.U32(uint32(P.IntMin));
		W.U32(uint32(P.IntMax));
	}
	TArray<FVoxelLandGraphNodeRecord> Nodes = Snapshot.Nodes;
	Nodes.Sort([](const FVoxelLandGraphNodeRecord& A, const FVoxelLandGraphNodeRecord& B) {
		return FVoxelLandProgramCodecFormat::LessGuid(A.Id, B.Id);
	});
	W.U32(Nodes.Num());
	for (const FVoxelLandGraphNodeRecord& N : Nodes)
	{
		FVoxelLandProgramCodecFormat::WriteGuid(W, N.Id);
		FVoxelLandProgramCodecFormat::WriteGuid(W, N.OperatorId);
		W.U32(uint32(N.OperatorVersion));
		W.U32(uint32(N.SeedSalt));
		FVoxelLandProgramCodecFormat::WriteGuid(W, N.ParameterId);
		W.U32(N.Configuration.Num());
		W.Bytes.Append(N.Configuration);
		W.U32(N.Pins.Num());
		for (const FVoxelLandGraphPinRecord& Pin : N.Pins)
		{
			FVoxelLandProgramCodecFormat::WriteGuid(W, Pin.Id);
			W.U8(uint8(Pin.Type));
			W.U8(Pin.bOutput ? 1 : 0);
			W.Float(Pin.FloatDefault);
			W.U32(uint32(Pin.IntDefault));
			W.U8(Pin.BoolDefault ? 1 : 0);
			W.U8(Pin.bInvalidDefault ? 1 : 0);
		}
	}
	TArray<FVoxelLandGraphLinkRecord> Links = Snapshot.Links;
	Links.Sort([](const FVoxelLandGraphLinkRecord& A, const FVoxelLandGraphLinkRecord& B) {
		if (A.TargetNode != B.TargetNode)
		{
			return FVoxelLandProgramCodecFormat::LessGuid(A.TargetNode, B.TargetNode);
		}
		if (A.TargetPin != B.TargetPin)
		{
			return FVoxelLandProgramCodecFormat::LessGuid(A.TargetPin, B.TargetPin);
		}
		if (A.SourceNode != B.SourceNode)
		{
			return FVoxelLandProgramCodecFormat::LessGuid(A.SourceNode, B.SourceNode);
		}
		return FVoxelLandProgramCodecFormat::LessGuid(A.SourcePin, B.SourcePin);
	});
	W.U32(Links.Num());
	for (const FVoxelLandGraphLinkRecord& Link : Links)
	{
		FVoxelLandProgramCodecFormat::WriteGuid(W, Link.SourceNode);
		FVoxelLandProgramCodecFormat::WriteGuid(W, Link.SourcePin);
		FVoxelLandProgramCodecFormat::WriteGuid(W, Link.TargetNode);
		FVoxelLandProgramCodecFormat::WriteGuid(W, Link.TargetPin);
	}
	// Preserve the v1 byte stream exactly for graphs without function calls.
	const bool bHasFunctionCalls = Snapshot.Nodes.ContainsByPredicate(
		[](const FVoxelLandGraphNodeRecord& Node) { return !Node.FunctionPath.IsNull(); });
	if (bHasFunctionCalls || !Snapshot.Functions.IsEmpty())
	{
		W.U32(0x31464c56); // Versioned function dependency extension.
		W.U32(Nodes.Num());
		for (const FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			FVoxelLandProgramCodecFormat::WriteGuid(W, Node.Id);
			W.String(Node.FunctionPath.ToString());
			W.U32(Node.Pins.Num());
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				FVoxelLandProgramCodecFormat::WriteGuid(W, Pin.InterfaceId);
			}
		}
		TArray<FVoxelLandGraphFunctionSnapshot> Functions = Snapshot.Functions;
		Functions.Sort([](const FVoxelLandGraphFunctionSnapshot& A, const FVoxelLandGraphFunctionSnapshot& B) {
			return A.AssetPath < B.AssetPath;
		});
		W.U32(Functions.Num());
		for (const FVoxelLandGraphFunctionSnapshot& Function : Functions)
		{
			W.String(Function.AssetPath);
			W.U32(uint32(Function.SourceVersion));
			W.U8(uint8(Function.OutputType));
			FVoxelLandProgramCodecFormat::WriteGuid(W, Function.OutputNode);
			FVoxelLandProgramCodecFormat::WriteGuid(W, Function.OutputPin);
			FVoxelLandGraphSnapshot FunctionSource;
			FunctionSource.SourceVersion = Function.SourceVersion;
			FunctionSource.Parameters = Function.Inputs;
			FunctionSource.Nodes = Function.Nodes;
			FunctionSource.Links = Function.Links;
			const uint64 FunctionHash = SourceFingerprint(FunctionSource);
			W.U32(uint32(FunctionHash));
			W.U32(uint32(FunctionHash >> 32));
		}
	}
	return FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
}

bool FVoxelLandProgramCodec::Pack(const FVoxelLandGraphSnapshot& Source, const FVoxelLandProgram& Program,
	FVoxelLandSerializedProgram& OutPayload, FString& OutError)
{
	OutPayload = FVoxelLandSerializedProgram();
	OutError.Reset();
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	FVoxelLandProgramDraft Recompiled;
	if (!FVoxelLandGraphCompiler::Compile(Source, Providers, Recompiled, OutError))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Checked;
	if (FVoxelLandError Error = FVoxelLandProgram::Validate(Recompiled, Providers, Checked))
	{
		OutError = Error.Message;
		return false;
	}
	if (Checked->GetFingerprint() != Program.GetFingerprint())
	{
		OutError = TEXT("Program does not match the source graph");
		return false;
	}
	const FVoxelLandProgramDraft& Draft = Program.GetData();
	FVoxelLandSerializedProgram Payload;
	Payload.ProgramVersion = int32(Draft.FormatVersion);
	Payload.ExternalFilterIndex = Draft.ExternalFilterIndex;
	Payload.CompilerSemanticVersion = int32(FVoxelLandProgramDraft::CompilerSemanticVersion);
	Payload.RegisterCount = Draft.RegisterCount;
	Payload.SourceFingerprint = FVoxelLandProgramCodecFormat::SignedBits(SourceFingerprint(Source));
	Payload.ProgramFingerprint = FVoxelLandProgramCodecFormat::SignedBits(Program.GetFingerprint());
	for (const FVoxelLandInstruction& I : Draft.Instructions)
	{
		FVoxelLandSerializedInstruction Record;
		Record.Opcode = int32(I.Op);
		Record.Dst = I.Dst;
		Record.A = I.A;
		Record.B = I.B;
		Record.C = I.C;
		Record.D = I.D;
		Record.E = I.E;
		Record.F = I.F;
		Record.Immediate = int32(I.Imm);
		Payload.Instructions.Add(Record);
	}
	Payload.Constants = Draft.Constants;
	for (const FVoxelLandParameterSlot& P : Draft.Parameters)
	{
		const FVoxelLandGraphParameter* Match = nullptr;
		for (const FVoxelLandGraphParameter& Candidate : Source.Parameters)
		{
			if (Candidate.Name.Equals(P.Name, ESearchCase::IgnoreCase))
			{
				if (Match)
				{
					OutError = TEXT("Duplicate source parameter name");
					return false;
				}
				Match = &Candidate;
			}
		}
		if (!Match)
		{
			OutError = TEXT("Program parameter is absent from source graph");
			return false;
		}
		FVoxelLandSerializedParameter Record;
		Record.Id = Match->Id;
		Record.Name = P.Name;
		Record.Type = uint8(P.Type);
		Record.FloatDefault = P.FloatDefault;
		Record.IntDefault = P.IntDefault;
		Payload.Parameters.Add(Record);
	}
	for (const FVoxelLandSeedSlot& Seed : Draft.Seeds)
	{
		FVoxelLandSerializedSeed Record;
		Record.NodeId = Seed.NodeId;
		Record.Salt = int32(Seed.Salt);
		Payload.Seeds.Add(Record);
	}
	for (const FVoxelLandNoiseConfig& Noise : Draft.Noise)
	{
		FVoxelLandSerializedNoise Record;
		Record.SeedSlot = int32(Noise.SeedSlot);
		Record.Frequency = FVoxelLandProgramCodecFormat::PackSource(Noise.Frequency);
		Record.Octaves = FVoxelLandProgramCodecFormat::PackSource(Noise.Octaves);
		Record.Gain = FVoxelLandProgramCodecFormat::PackSource(Noise.Gain);
		Record.Lacunarity = FVoxelLandProgramCodecFormat::PackSource(Noise.Lacunarity);
		Payload.Noise.Add(Record);
	}
	Payload.ExternalOperators = Draft.ExternalOperators;
	Payload.SmoothFilter.bPresent = Draft.SmoothFilter.bPresent;
	Payload.SmoothFilter.Enabled = FVoxelLandProgramCodecFormat::PackSource(Draft.SmoothFilter.Enabled);
	Payload.SmoothFilter.Strength = FVoxelLandProgramCodecFormat::PackSource(Draft.SmoothFilter.Strength);
	OutPayload = MoveTemp(Payload);
	return true;
}

class FVoxelLandProgramCodecFingerprint final
{
public:
	static bool DecodeInstructions(
		const FVoxelLandSerializedProgram& Payload, FVoxelLandProgramDraft& Draft, FString& OutError);
	static bool DecodeParameters(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
		FVoxelLandProgramDraft& Draft, FString& OutError);
};

bool FVoxelLandProgramCodecFingerprint::DecodeInstructions(
	const FVoxelLandSerializedProgram& Payload, FVoxelLandProgramDraft& Draft, FString& OutError)
{
	for (const FVoxelLandSerializedInstruction& Record : Payload.Instructions)
	{
		if (Record.Opcode < 0 || Record.Opcode > int32(EVoxelLandOpcode::ExternalOperator) || Record.Dst < 0
			|| Record.Dst > 65535 || Record.A < 0 || Record.A > 65535 || Record.B < 0 || Record.B > 65535
			|| Record.C < 0 || Record.C > 65535 || Record.D < 0 || Record.D > 65535 || Record.E < 0 || Record.E > 65535
			|| Record.F < 0 || Record.F > 65535)
		{
			OutError = TEXT("Invalid serialized instruction");
			return false;
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
	return true;
}

bool FVoxelLandProgramCodecFingerprint::DecodeParameters(const FVoxelLandGraphSnapshot& Source,
	const FVoxelLandSerializedProgram& Payload, FVoxelLandProgramDraft& Draft, FString& OutError)
{
	Draft.Constants = Payload.Constants;
	TSet<FGuid> ParameterIds;
	for (const FVoxelLandSerializedParameter& Record : Payload.Parameters)
	{
		const FVoxelLandGraphParameter* SourceParameter = Source.Parameters.FindByPredicate(
			[&Record](const FVoxelLandGraphParameter& P) { return P.Id == Record.Id; });
		if (!SourceParameter || ParameterIds.Contains(Record.Id)
			|| !SourceParameter->Name.Equals(Record.Name, ESearchCase::IgnoreCase)
			|| uint8(SourceParameter->Type) != Record.Type)
		{
			OutError = TEXT("Compiled parameter mapping does not match source");
			return false;
		}
		ParameterIds.Add(Record.Id);
		FVoxelLandParameterSlot P;
		P.Name = Record.Name;
		P.Type = EVoxelLandParameterType(Record.Type);
		P.FloatDefault = Record.FloatDefault;
		P.IntDefault = Record.IntDefault;
		Draft.Parameters.Add(P);
	}
	return true;
}

bool FVoxelLandProgramCodec::Load(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram, FString& OutError)
{
	const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
	return Load(Source, Payload, Providers, OutProgram, OutError);
}

bool FVoxelLandProgramCodec::Load(const FVoxelLandGraphSnapshot& Source, const FVoxelLandSerializedProgram& Payload,
	const FVoxelLandOperatorRegistrySnapshot& Providers,
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram, FString& OutError)
{
	OutProgram.Reset();
	OutError.Reset();
	const bool bHasFunctions =
		!Source.Functions.IsEmpty() || Source.Nodes.ContainsByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
			return Node.OperatorId == VoxelLandGraphFunctionCallId() || !Node.FunctionPath.IsNull();
		});
	if (bHasFunctions)
	{
		FVoxelLandGraphDiagnostic Diagnostic;
		if (!FVoxelLandGraphFunctionFlatten::ValidateSource(Source, Diagnostic))
		{
			OutError = Diagnostic.Message;
			return false;
		}
	}
	if (Payload.CompilerSemanticVersion != int32(FVoxelLandProgramDraft::CompilerSemanticVersion))
	{
		OutError = TEXT("Compiled graph has an incompatible compiler semantic version; recompile and save the graph");
		return false;
	}
	if (Payload.EnvelopeVersion != 1 || Payload.ProgramVersion != int32(FVoxelLandProgramDraft::CurrentFormatVersion)
		|| Payload.RegisterCount < 1 || Payload.RegisterCount > 1024 || Payload.Instructions.Num() > 8192
		|| Payload.Constants.Num() > 65536 || Payload.Parameters.Num() > 4096 || Payload.Seeds.Num() > 4096
		|| Payload.Noise.Num() > 4096 || Payload.ExternalOperators.Num() > 4096
		|| FVoxelLandProgramCodecFormat::UnsignedBits(Payload.SourceFingerprint) != SourceFingerprint(Source))
	{
		OutError = TEXT("Compiled payload is stale, unsupported or exceeds limits");
		return false;
	}
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = uint32(Payload.ProgramVersion);
	Draft.RegisterCount = Payload.RegisterCount;
	Draft.ExternalFilterIndex = Payload.ExternalFilterIndex;
	Draft.ExternalOperators = Payload.ExternalOperators;
	Draft.SmoothFilter.bPresent = Payload.SmoothFilter.bPresent;
	Draft.SmoothFilter.Enabled = FVoxelLandProgramCodecFormat::LoadSource(Payload.SmoothFilter.Enabled);
	Draft.SmoothFilter.Strength = FVoxelLandProgramCodecFormat::LoadSource(Payload.SmoothFilter.Strength);
	if (!FVoxelLandProgramCodecFingerprint::DecodeInstructions(Payload, Draft, OutError)
		|| !FVoxelLandProgramCodecFingerprint::DecodeParameters(Source, Payload, Draft, OutError))
	{
		return false;
	}
	for (const FVoxelLandSerializedSeed& Record : Payload.Seeds)
	{
		FVoxelLandSeedSlot Seed;
		Seed.NodeId = Record.NodeId;
		Seed.Salt = uint32(Record.Salt);
		Draft.Seeds.Add(Seed);
	}
	for (const FVoxelLandSerializedNoise& Record : Payload.Noise)
	{
		FVoxelLandNoiseConfig Noise;
		Noise.SeedSlot = uint32(Record.SeedSlot);
		Noise.Frequency = FVoxelLandProgramCodecFormat::LoadSource(Record.Frequency);
		Noise.Octaves = FVoxelLandProgramCodecFormat::LoadSource(Record.Octaves);
		Noise.Gain = FVoxelLandProgramCodecFormat::LoadSource(Record.Gain);
		Noise.Lacunarity = FVoxelLandProgramCodecFormat::LoadSource(Record.Lacunarity);
		Draft.Noise.Add(Noise);
	}
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Providers, Program))
	{
		OutError = Error.Message;
		return false;
	}
	if (Program->GetFingerprint() != FVoxelLandProgramCodecFormat::UnsignedBits(Payload.ProgramFingerprint))
	{
		OutError = TEXT("Compiled program fingerprint mismatch");
		return false;
	}
	OutProgram = MoveTemp(Program);
	return true;
}
