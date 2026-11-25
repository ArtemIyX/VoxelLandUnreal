#include "Graph/Serialization/VoxelLandCaveSourceFingerprint.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "VoxelLandCaveFingerprintGraphView.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Hash/xxhash.h"

class FVoxelLandM5SourceFingerprintInternals final
{
public:
	static constexpr int32 M5FingerprintMaxRecords = 4096;
	static constexpr int64 M5FingerprintMaxBytes = 4 * 1024 * 1024;

	static FVoxelLandM5Diagnostic M5FingerprintGraph(const FVoxelLandM5FingerprintGraphView& Graph,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		uint64& OutFingerprint);

	static FVoxelLandM5Diagnostic M5FingerprintError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("M5.SourceFingerprint");
		D.Message = Message;
		return D;
	}

	static bool M5FingerprintLessGuid(const FGuid& A, const FGuid& B)
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

	static void M5FingerprintWriteGuid(FCanonicalWriter& W, const FGuid& G)
	{
		W.U32(G.A);
		W.U32(G.B);
		W.U32(G.C);
		W.U32(G.D);
	}

	static bool M5FingerprintLessLink(const FVoxelLandGraphLinkRecord& A, const FVoxelLandGraphLinkRecord& B)
	{
		if (A.TargetNode != B.TargetNode)
		{
			return M5FingerprintLessGuid(A.TargetNode, B.TargetNode);
		}
		if (A.TargetPin != B.TargetPin)
		{
			return M5FingerprintLessGuid(A.TargetPin, B.TargetPin);
		}
		if (A.SourceNode != B.SourceNode)
		{
			return M5FingerprintLessGuid(A.SourceNode, B.SourceNode);
		}
		return M5FingerprintLessGuid(A.SourcePin, B.SourcePin);
	}

	static bool M5FingerprintAscii(const FString& Value)
	{
		for (TCHAR C : Value)
		{
			if (C <= 0 || C > 127)
			{
				return false;
			}
		}
		return true;
	}

	// Bound the path before materializing it. UE 5.7 stores the subpath as UTF-8
	// and the package/asset components as FNames; no UObject is loaded here.
	static bool M5FingerprintPath(const FSoftObjectPath& Path, FString& Out)
	{
		if (Path.IsNull())
		{
			Out.Reset();
			return true;
		}
		const FTopLevelAssetPath Asset = Path.GetAssetPath();
		const uint64 Upper = uint64(Asset.GetPackageName().GetStringLength())
						   + uint64(Asset.GetAssetName().GetStringLength()) + uint64(Path.GetSubPathUtf8String().Len())
						   + 2;
		if (Upper > 2048)
		{
			return false;
		}
		Out = Path.ToString();
		return !Out.IsEmpty() && Out.Len() <= 2048 && M5FingerprintAscii(Out);
	}

	static void M5FingerprintWriteAscii(FCanonicalWriter& W, const FString& Value)
	{
		W.U32(uint32(Value.Len()));
		for (TCHAR C : Value)
		{
			W.U8(uint8(C));
		}
	}

	// A flat captured dependency table contains the transitive function closure.
	// Validate reachability and cycles without copying any graph or loading assets.
	static FVoxelLandM5Diagnostic M5FingerprintValidateFunctions(const FVoxelLandGraphSnapshot& Source,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
	{
		using namespace VoxelLand::M5::CheckedMath;
		const int32 Count = Source.Functions.Num();
		if (Count > 128)
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function count exceeds limit"));
		}
		if (!Budget->TryChargeWork(int64(Count) * 4096 + 1))
		{
			return M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function preflight work exceeds limit"));
		}
		int64 TotalNodes = Source.Nodes.Num();
		int64 TotalRecords = int64(Source.Parameters.Num()) + Source.Nodes.Num() + Source.Links.Num();
		for (const FVoxelLandGraphFunctionSnapshot& F : Source.Functions)
		{
			if (IsCancelled())
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (F.AssetPath.IsEmpty() || F.AssetPath.Len() > 2048 || !M5FingerprintAscii(F.AssetPath)
				|| F.Inputs.Num() > M5FingerprintMaxRecords || F.Nodes.Num() > M5FingerprintMaxRecords
				|| F.Links.Num() > M5FingerprintMaxRecords || !TryAdd(TotalNodes, F.Nodes.Num(), TotalNodes)
				|| !TryAdd(TotalRecords, int64(F.Inputs.Num()) + F.Nodes.Num() + F.Links.Num(), TotalRecords)
				|| TotalNodes > M5FingerprintMaxRecords || TotalRecords > 3 * M5FingerprintMaxRecords)
			{
				return M5FingerprintError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid or oversized function dependency"));
			}
		}
		int64 Work = 0;
		int64 MatrixCells = 0;
		int64 MatrixWork = 0;
		// Each pair of paths may share all 2048 characters. Reserve two full
		// comparisons per pair in addition to transitive closure and polling.
		if (!TryMultiplyNonNegative(Count, Count, MatrixCells)
			|| !TryMultiplyNonNegative(MatrixCells, int64(Count) * 4 + 4100, MatrixWork)
			|| !TryMultiplyNonNegative(TotalNodes, int64(Count) + 1, Work) || !TryMultiplyNonNegative(Work, 4096, Work)
			|| !TryAdd(Work, MatrixWork, Work) || !TryAdd(Work, 128, Work) || !Budget->TryChargeWork(Work))
		{
			return M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function dependency work exceeds limit"));
		}
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(2 * MatrixCells + 2 * Count + 32768, Lease))
		{
			return M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function dependency memory exceeds limit"));
		}
		TArray<uint8> FunctionEdges;
		FunctionEdges.Init(0, int32(MatrixCells));
		TArray<uint8> Roots;
		Roots.Init(0, Count);
		for (int32 I = 0; I < Count; ++I)
		{
			for (int32 J = 0; J < I; ++J)
			{
				if (IsCancelled())
				{
					return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
				}
				if (Source.Functions[I].AssetPath == Source.Functions[J].AssetPath)
				{
					return M5FingerprintError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate function path"));
				}
			}
		}
		FString Path;
		auto VisitNodes = [&](const TArray<FVoxelLandGraphNodeRecord>& Nodes, int32 Parent) -> FVoxelLandM5Diagnostic {
			for (const FVoxelLandGraphNodeRecord& Node : Nodes)
			{
				if (IsCancelled())
				{
					return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
				}
				const bool bCall = Node.OperatorId == VoxelLandGraphFunctionCallId();
				if (bCall != !Node.FunctionPath.IsNull() || !M5FingerprintPath(Node.FunctionPath, Path))
				{
					return M5FingerprintError(
						EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid or unsupported function call path"));
				}
				if (!bCall)
				{
					continue;
				}
				int32 Target = INDEX_NONE;
				for (int32 I = 0; I < Count; ++I)
				{
					if (IsCancelled())
					{
						return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
					}
					if (Source.Functions[I].AssetPath == Path)
					{
						Target = I;
						break;
					}
				}
				if (Target == INDEX_NONE)
				{
					return M5FingerprintError(
						EVoxelLandM5ErrorCode::InvalidInput, TEXT("Missing captured function dependency"));
				}
				if (Parent == INDEX_NONE)
				{
					Roots[Target] = 1;
				}
				else
				{
					FunctionEdges[Parent * Count + Target] = 1;
				}
			}
			return {};
		};
		FVoxelLandM5Diagnostic D = VisitNodes(Source.Nodes, INDEX_NONE);
		if (D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
		for (int32 I = 0; I < Count; ++I)
		{
			D = VisitNodes(Source.Functions[I].Nodes, I);
			if (D.Code != EVoxelLandM5ErrorCode::None)
			{
				return D;
			}
		}
		TArray<uint8> Direct = FunctionEdges;
		TArray<uint8> Depth = Roots;
		for (int32 Pass = 0; Pass < Count; ++Pass)
		{
			bool bChanged = false;
			for (int32 I = 0; I < Count; ++I)
			{
				for (int32 J = 0; J < Count; ++J)
				{
					if (IsCancelled())
					{
						return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
					}
					if (Depth[I] && Direct[I * Count + J] && Depth[J] < Depth[I] + 1)
					{
						if (Depth[I] >= 16)
						{
							return M5FingerprintError(
								EVoxelLandM5ErrorCode::InvalidInput, TEXT("Function depth exceeds limit or cycle"));
						}
						Depth[J] = Depth[I] + 1;
						bChanged = true;
					}
				}
			}
			if (!bChanged)
			{
				break;
			}
		}
		for (int32 K = 0; K < Count; ++K)
		{
			for (int32 I = 0; I < Count; ++I)
			{
				for (int32 J = 0; J < Count; ++J)
				{
					if (IsCancelled())
					{
						return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
					}
					FunctionEdges[I * Count + J] |= FunctionEdges[I * Count + K] & FunctionEdges[K * Count + J];
				}
			}
		}
		for (int32 I = 0; I < Count; ++I)
		{
			if (FunctionEdges[I * Count + I])
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Function dependency cycle"));
			}
			bool bReachable = Roots[I] != 0;
			for (int32 J = 0; J < Count && !bReachable; ++J)
			{
				bReachable = Roots[J] && FunctionEdges[J * Count + I];
			}
			if (!bReachable)
			{
				return M5FingerprintError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Unreferenced function dependency"));
			}
		}
		return {};
	}

	template <typename T, typename TLess>
	static bool M5FingerprintSortIndices(
		const TArray<T>& Items, TLess Less, TFunctionRef<bool()> IsCancelled, TArray<int32>& Indices)
	{
		Indices.Reserve(Items.Num());
		for (int32 I = 0; I < Items.Num(); ++I)
		{
			if (IsCancelled())
			{
				return false;
			}
			Indices.Add(I);
			int32 J = I;
			while (J > 0 && Less(Items[Indices[J]], Items[Indices[J - 1]]))
			{
				if (IsCancelled())
				{
					return false;
				}
				Swap(Indices[J], Indices[J - 1]);
				--J;
			}
		}
		return true;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5SourceFingerprintInternals::M5FingerprintGraph(
	const FVoxelLandM5FingerprintGraphView& Graph, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, uint64& OutFingerprint)
{
	using namespace VoxelLand::M5::CheckedMath;
	const int32 SourceVersion = Graph.SourceVersion;
	const TArray<FVoxelLandGraphParameter>& Parameters = Graph.Parameters;
	const TArray<FVoxelLandGraphNodeRecord>& Nodes = Graph.Nodes;
	const TArray<FVoxelLandGraphLinkRecord>& Links = Graph.Links;
	const TArray<FVoxelLandGraphFunctionSnapshot>* const Functions = Graph.Functions;
	if (IsCancelled())
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (Parameters.Num() > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxRecords
		|| Nodes.Num() > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxRecords
		|| Links.Num() > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxRecords)
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source record limit exceeded"));
	}
	const int64 Records = int64(Parameters.Num()) + Nodes.Num() + Links.Num();
	if (!Budget->TryChargeWork(Records * 2 + 1))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source preflight work exhausted"));
	}
	bool bHasFunctionExtension = Functions && !Functions->IsEmpty();
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		bHasFunctionExtension |= !Node.FunctionPath.IsNull();
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> PathLease;
	if (bHasFunctionExtension
		&& (!Budget->TryChargeWork(int64(Nodes.Num()) * 4096 + 128) || !Budget->TryReserveBytes(32768, PathLease)))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function path preflight exceeds budget"));
	}
	int64 Bytes = 20; // version, source version, and three array lengths
	int64 VariableItems = 0;
	for (const FVoxelLandGraphParameter& P : Parameters)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (!TryAdd(VariableItems, P.Name.Len(), VariableItems) || !TryAdd(Bytes, 47 + int64(P.Name.Len()), Bytes)
			|| Bytes > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes
			|| VariableItems > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes)
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Parameter bytes exceed limit"));
		}
	}
	for (const FVoxelLandGraphNodeRecord& N : Nodes)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		int64 PinBytes = 0;
		if (!TryMultiplyNonNegative(N.Pins.Num(), 28, PinBytes) || !TryAdd(Bytes, 64, Bytes)
			|| !TryAdd(Bytes, N.Configuration.Num(), Bytes) || !TryAdd(Bytes, PinBytes, Bytes)
			|| !TryAdd(VariableItems, N.Configuration.Num(), VariableItems)
			|| !TryAdd(VariableItems, N.Pins.Num(), VariableItems)
			|| Bytes > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes
			|| VariableItems > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes)
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Node bytes exceed limit"));
		}
	}
	if (bHasFunctionExtension)
	{
		if (!TryAdd(Bytes, 12, Bytes))
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function extension exceeds limit"));
		}
		FString Path;
		for (const FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			int64 PinBytes = 0;
			if (!FVoxelLandM5SourceFingerprintInternals::M5FingerprintPath(Node.FunctionPath, Path)
				|| !TryMultiplyNonNegative(Node.Pins.Num(), 16, PinBytes)
				|| !TryAdd(Bytes, 24 + int64(Path.Len()), Bytes) || !TryAdd(Bytes, PinBytes, Bytes)
				|| Bytes > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes)
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Unsupported function path or extension size"));
			}
		}
		if (Functions)
		{
			for (const FVoxelLandGraphFunctionSnapshot& F : *Functions)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
				}
				if (!TryAdd(Bytes, 49 + int64(F.AssetPath.Len()), Bytes)
					|| Bytes > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes)
				{
					return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
						EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function metadata exceeds limit"));
				}
			}
		}
	}
	int64 LinkBytes = 0;
	if (!TryMultiplyNonNegative(Links.Num(), 64, LinkBytes) || !TryAdd(Bytes, LinkBytes, Bytes)
		|| Bytes > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxBytes
		|| !Budget->TryChargeWork(VariableItems))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source bytes or work exceed limit"));
	}
	// The legacy writer lowercases names and converts them to UTF-8. Restricting
	// this first metered path to ASCII makes its byte length and allocation bound
	// exact and avoids creating temporary lower-case/UTF-8 strings.
	for (const FVoxelLandGraphParameter& P : Parameters)
	{
		for (int32 I = 0; I < P.Name.Len(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (P.Name[I] <= 0 || P.Name[I] > 127)
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Only ASCII parameter names are metered"));
			}
		}
	}
	int64 SortWork = 0;
	int64 Work = 0;
	int64 IndexBytes = 0;
	int64 ScratchBytes = 0;
	if (!TryMultiplyNonNegative(Records, Records, SortWork) || !TryMultiplyNonNegative(SortWork, 3, SortWork)
		|| !TryAdd(SortWork, Bytes, Work) || !TryAdd(Work, Records * 8 + 64, Work)
		|| !TryMultiplyNonNegative(Records, sizeof(int32), IndexBytes) || !TryAdd(Bytes, IndexBytes, ScratchBytes)
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes)
		|| (Functions
			&& (!TryAdd(Work, int64(Functions->Num()) * Functions->Num() * 4096, Work)
				|| !TryAdd(ScratchBytes, int64(Functions->Num()) * sizeof(int32), ScratchBytes)))
		|| !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fingerprint work exceeds limit"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ScratchBytes, Lease))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fingerprint memory exceeds limit"));
	}
	TArray<int32> ParameterOrder, NodeOrder, LinkOrder;
	if (!FVoxelLandM5SourceFingerprintInternals::M5FingerprintSortIndices(
			Parameters,
			[](const FVoxelLandGraphParameter& A, const FVoxelLandGraphParameter& B) {
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintLessGuid(A.Id, B.Id);
			},
			IsCancelled, ParameterOrder)
		|| !FVoxelLandM5SourceFingerprintInternals::M5FingerprintSortIndices(
			Nodes,
			[](const FVoxelLandGraphNodeRecord& A, const FVoxelLandGraphNodeRecord& B) {
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintLessGuid(A.Id, B.Id);
			},
			IsCancelled, NodeOrder)
		|| !FVoxelLandM5SourceFingerprintInternals::M5FingerprintSortIndices(
			Links, FVoxelLandM5SourceFingerprintInternals::M5FingerprintLessLink, IsCancelled, LinkOrder))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	for (int32 I = 1; I < ParameterOrder.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (Parameters[ParameterOrder[I - 1]].Id == Parameters[ParameterOrder[I]].Id)
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate parameter ID"));
		}
	}
	for (int32 I = 1; I < NodeOrder.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (Nodes[NodeOrder[I - 1]].Id == Nodes[NodeOrder[I]].Id)
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate node ID"));
		}
	}
	FCanonicalWriter W;
	W.Bytes.Reserve(int32(Bytes));
	W.U32(1);
	W.U32(uint32(SourceVersion));
	W.U32(uint32(ParameterOrder.Num()));
	for (int32 Index : ParameterOrder)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphParameter& P = Parameters[Index];
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, P.Id);
		W.U32(uint32(P.Name.Len()));
		for (int32 I = 0; I < P.Name.Len(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const TCHAR C = P.Name[I];
			W.U8(uint8(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C));
		}
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
	W.U32(uint32(NodeOrder.Num()));
	for (int32 Index : NodeOrder)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphNodeRecord& N = Nodes[Index];
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, N.Id);
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, N.OperatorId);
		W.U32(uint32(N.OperatorVersion));
		W.U32(uint32(N.SeedSalt));
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, N.ParameterId);
		W.U32(uint32(N.Configuration.Num()));
		for (int32 Offset = 0; Offset < N.Configuration.Num(); Offset += 4096)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const int32 Count = FMath::Min(4096, N.Configuration.Num() - Offset);
			W.Bytes.Append(N.Configuration.GetData() + Offset, Count);
		}
		W.U32(uint32(N.Pins.Num()));
		for (int32 I = 0; I < N.Pins.Num(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const FVoxelLandGraphPinRecord& Pin = N.Pins[I];
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Pin.Id);
			W.U8(uint8(Pin.Type));
			W.U8(Pin.bOutput ? 1 : 0);
			W.Float(Pin.FloatDefault);
			W.U32(uint32(Pin.IntDefault));
			W.U8(Pin.BoolDefault ? 1 : 0);
			W.U8(Pin.bInvalidDefault ? 1 : 0);
		}
	}
	W.U32(uint32(LinkOrder.Num()));
	for (int32 Index : LinkOrder)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphLinkRecord& Link = Links[Index];
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Link.SourceNode);
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Link.SourcePin);
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Link.TargetNode);
		FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Link.TargetPin);
	}
	if (bHasFunctionExtension)
	{
		W.U32(0x31464c56);
		W.U32(uint32(NodeOrder.Num()));
		FString Path;
		for (int32 Index : NodeOrder)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const FVoxelLandGraphNodeRecord& Node = Nodes[Index];
			if (!FVoxelLandM5SourceFingerprintInternals::M5FingerprintPath(Node.FunctionPath, Path))
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Unsupported function path"));
			}
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Node.Id);
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteAscii(W, Path);
			W.U32(uint32(Node.Pins.Num()));
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
				}
				FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, Pin.InterfaceId);
			}
		}
		TArray<int32> FunctionOrder;
		if (Functions
			&& !FVoxelLandM5SourceFingerprintInternals::M5FingerprintSortIndices(
				*Functions,
				[](const FVoxelLandGraphFunctionSnapshot& A, const FVoxelLandGraphFunctionSnapshot& B) {
					return A.AssetPath < B.AssetPath;
				},
				IsCancelled, FunctionOrder))
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		W.U32(uint32(FunctionOrder.Num()));
		for (int32 Index : FunctionOrder)
		{
			if (IsCancelled())
			{
				return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const FVoxelLandGraphFunctionSnapshot& F = (*Functions)[Index];
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteAscii(W, F.AssetPath);
			W.U32(uint32(F.SourceVersion));
			W.U8(uint8(F.OutputType));
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, F.OutputNode);
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintWriteGuid(W, F.OutputPin);
			uint64 Hash = 0;
			const FVoxelLandM5Diagnostic D =
				M5FingerprintGraph({F.SourceVersion, F.Inputs, F.Nodes, F.Links}, Budget, IsCancelled, Hash);
			if (D.Code != EVoxelLandM5ErrorCode::None)
			{
				return D;
			}
			W.U32(uint32(Hash));
			W.U32(uint32(Hash >> 32));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (W.Bytes.Num() != Bytes)
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Canonical byte count mismatch"));
	}
	const uint64 Result = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
	if (IsCancelled())
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	OutFingerprint = Result;
	return {};
}

FVoxelLandM5Diagnostic VoxelLand::M5::SourceFingerprint::Compute(const FVoxelLandGraphSnapshot& Source,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	uint64& OutFingerprint)
{
	if (Source.Nodes.Num() > FVoxelLandM5SourceFingerprintInternals::M5FingerprintMaxRecords)
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source node count exceeds limit"));
	}
	// Charge the complete bounded function-detection scan before examining any
	// records. An empty source retains the existing empty-graph accounting.
	if (!Source.Nodes.IsEmpty() && !Budget->TryChargeWork(Source.Nodes.Num()))
	{
		return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Function detection work exceeds limit"));
	}
	bool bValidateFunctions = !Source.Functions.IsEmpty();
	for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5SourceFingerprintInternals::M5FingerprintError(
				EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		bValidateFunctions |= !Node.FunctionPath.IsNull() || Node.OperatorId == VoxelLandGraphFunctionCallId();
	}
	if (bValidateFunctions)
	{
		const FVoxelLandM5Diagnostic D =
			FVoxelLandM5SourceFingerprintInternals::M5FingerprintValidateFunctions(Source, Budget, IsCancelled);
		if (D.Code != EVoxelLandM5ErrorCode::None)
		{
			return D;
		}
	}
	return FVoxelLandM5SourceFingerprintInternals::M5FingerprintGraph(
		{Source.SourceVersion, Source.Parameters, Source.Nodes, Source.Links, &Source.Functions}, Budget, IsCancelled,
		OutFingerprint);
}