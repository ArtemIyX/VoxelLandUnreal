#include "Graph/TerrainStages/VoxelLandHeightGraphStageFingerprint.h"
#include "Graph/TerrainStages/VoxelLandHeightGraphStageAnalysis.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Hash/xxhash.h"

namespace VoxelLandM4StageFingerprint
{
class FVoxelLandM4StageFingerprintInternals final
{
public:
	static constexpr int32 MaxSemanticRecords = 4096;
	static constexpr int32 MaxSemanticBytes = 65536;
	static constexpr int64 MaxTotalBytes = 8 * 1024 * 1024;
	static FVoxelLandError FingerprintFailure(const TCHAR* Message);
	static bool FingerprintGuidLess(const FGuid& A, const FGuid& B);
	static void FingerprintGuid(FCanonicalWriter& Writer, const FGuid& Guid);
};

FVoxelLandError FVoxelLandM4StageFingerprintInternals::FingerprintFailure(const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = EVoxelLandErrorCode::InvalidProgram;
	Error.Message = Message;
	return Error;
}

bool FVoxelLandM4StageFingerprintInternals::FingerprintGuidLess(const FGuid& A, const FGuid& B)
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

void FVoxelLandM4StageFingerprintInternals::FingerprintGuid(FCanonicalWriter& Writer, const FGuid& Guid)
{
	// Each uint32 word is little endian; order is A, B, C, D.
	Writer.U32(Guid.A);
	Writer.U32(Guid.B);
	Writer.U32(Guid.C);
	Writer.U32(Guid.D);
}

FVoxelLandError Build(TConstArrayView<FVoxelLandM4StageNode> Nodes, TConstArrayView<FVoxelLandM4StageOutput> Outputs,
	const FString& OutputName, TConstArrayView<FVoxelLandM4StageFingerprintNode> Semantics,
	TConstArrayView<uint8> Context, TArray<uint8>& OutBytes, uint64& OutHash)
{
	TArray<FGuid> Closure;
	FVoxelLandError Error = VoxelLandM4StageAnalysis::CollectGlobalDependencies(Nodes, Outputs, OutputName, Closure);
	if (Error)
	{
		return Error;
	}
	if (Semantics.Num() > FVoxelLandM4StageFingerprintInternals::MaxSemanticRecords
		|| Context.Num() > FVoxelLandM4StageFingerprintInternals::MaxSemanticBytes)
	{
		return FVoxelLandM4StageFingerprintInternals::FingerprintFailure(TEXT("M4 fingerprint input exceeds limits"));
	}

	TMap<FGuid, const FVoxelLandM4StageNode*> ById;
	for (const FVoxelLandM4StageNode& Node : Nodes)
	{
		ById.Add(Node.Id, &Node);
	}
	TMap<FGuid, const FVoxelLandM4StageFingerprintNode*> SemanticById;
	int64 Total = Context.Num();
	for (const FVoxelLandM4StageFingerprintNode& Record : Semantics)
	{
		if (!ById.Contains(Record.Id) || SemanticById.Contains(Record.Id) || Record.SemanticsVersion == 0
			|| Record.SemanticBytes.IsEmpty()
			|| Record.SemanticBytes.Num() > FVoxelLandM4StageFingerprintInternals::MaxSemanticBytes)
		{
			return FVoxelLandM4StageFingerprintInternals::FingerprintFailure(
				TEXT("Invalid or duplicate M4 node semantics"));
		}
		Total += Record.SemanticBytes.Num();
		if (Total > FVoxelLandM4StageFingerprintInternals::MaxTotalBytes)
		{
			return FVoxelLandM4StageFingerprintInternals::FingerprintFailure(TEXT("M4 semantic bytes exceed limit"));
		}
		SemanticById.Add(Record.Id, &Record);
	}
	for (const FGuid& Id : Closure)
	{
		if (!SemanticById.Contains(Id))
		{
			return FVoxelLandM4StageFingerprintInternals::FingerprintFailure(
				TEXT("Missing M4 upstream node semantics"));
		}
	}

	FCanonicalWriter Writer;
	Writer.U8('V');
	Writer.U8('L');
	Writer.U8('S');
	Writer.U8('F');
	Writer.U32(1); // encoding format, not terrain algorithm or program version
	FVoxelLandM4StageFingerprintInternals::FingerprintGuid(Writer, Closure.Last()); // requested output producer
	Writer.U32(uint32(Context.Num()));
	Writer.Bytes.Append(Context.GetData(), Context.Num());
	Writer.U32(uint32(Closure.Num()));
	for (const FGuid& Id : Closure)
	{
		const FVoxelLandM4StageNode& Node = *ById.FindChecked(Id);
		const FVoxelLandM4StageFingerprintNode& Record = *SemanticById.FindChecked(Id);
		FVoxelLandM4StageFingerprintInternals::FingerprintGuid(Writer, Id);
		Writer.U8(uint8(Node.Stage));
		Writer.U8(Node.bUsesZ ? 1 : 0);
		Writer.U32(Record.SemanticsVersion);
		Writer.U32(uint32(Record.SemanticBytes.Num()));
		Writer.Bytes.Append(Record.SemanticBytes);
		TArray<FGuid> Dependencies = Node.Dependencies;
		Dependencies.Sort(FVoxelLandM4StageFingerprintInternals::FingerprintGuidLess);
		Writer.U32(uint32(Dependencies.Num()));
		for (const FGuid& Dependency : Dependencies)
		{
			FVoxelLandM4StageFingerprintInternals::FingerprintGuid(Writer, Dependency);
		}
	}
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	OutBytes = MoveTemp(Writer.Bytes);
	OutHash = Hash;
	return {};
}
}