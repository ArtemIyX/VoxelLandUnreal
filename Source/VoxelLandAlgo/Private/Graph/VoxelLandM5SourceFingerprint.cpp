#include "Graph/VoxelLandM5SourceFingerprint.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Hash/xxhash.h"

namespace
{
constexpr int32 M5FingerprintMaxRecords = 4096;
constexpr int64 M5FingerprintMaxBytes = 4 * 1024 * 1024;

FVoxelLandM5Diagnostic M5FingerprintError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic D;
	D.Code = Code;
	D.Stage = TEXT("M5.SourceFingerprint");
	D.Message = Message;
	return D;
}

bool M5FingerprintLessGuid(const FGuid& A, const FGuid& B)
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

void M5FingerprintWriteGuid(FCanonicalWriter& W, const FGuid& G)
{
	W.U32(G.A);
	W.U32(G.B);
	W.U32(G.C);
	W.U32(G.D);
}

bool M5FingerprintLessLink(const FVoxelLandGraphLinkRecord& A, const FVoxelLandGraphLinkRecord& B)
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

template <typename T, typename TLess>
bool M5FingerprintSortIndices(
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
}

FVoxelLandM5Diagnostic VoxelLand::M5::SourceFingerprint::Compute(const FVoxelLandGraphSnapshot& Source,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	uint64& OutFingerprint)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (!Source.Functions.IsEmpty())
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Unproven, TEXT("Function dependencies are not metered"));
	}
	if (Source.Parameters.Num() > M5FingerprintMaxRecords || Source.Nodes.Num() > M5FingerprintMaxRecords
		|| Source.Links.Num() > M5FingerprintMaxRecords)
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source record limit exceeded"));
	}
	const int64 Records = int64(Source.Parameters.Num()) + Source.Nodes.Num() + Source.Links.Num();
	if (!Budget->TryChargeWork(Records * 2 + 1))
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source preflight work exhausted"));
	}
	int64 Bytes = 20; // version, source version, and three array lengths
	int64 VariableItems = 0;
	for (const FVoxelLandGraphParameter& P : Source.Parameters)
	{
		if (IsCancelled())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (!TryAdd(VariableItems, P.Name.Len(), VariableItems) || !TryAdd(Bytes, 47 + int64(P.Name.Len()), Bytes)
			|| Bytes > M5FingerprintMaxBytes || VariableItems > M5FingerprintMaxBytes)
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Parameter bytes exceed limit"));
		}
	}
	for (const FVoxelLandGraphNodeRecord& N : Source.Nodes)
	{
		if (IsCancelled())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (!N.FunctionPath.IsNull() || N.OperatorId == VoxelLandGraphFunctionCallId())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Unproven, TEXT("Function calls are not metered"));
		}
		int64 PinBytes = 0;
		if (!TryMultiplyNonNegative(N.Pins.Num(), 28, PinBytes) || !TryAdd(Bytes, 64, Bytes)
			|| !TryAdd(Bytes, N.Configuration.Num(), Bytes) || !TryAdd(Bytes, PinBytes, Bytes)
			|| !TryAdd(VariableItems, N.Configuration.Num(), VariableItems)
			|| !TryAdd(VariableItems, N.Pins.Num(), VariableItems) || Bytes > M5FingerprintMaxBytes
			|| VariableItems > M5FingerprintMaxBytes)
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Node bytes exceed limit"));
		}
	}
	int64 LinkBytes = 0;
	if (!TryMultiplyNonNegative(Source.Links.Num(), 64, LinkBytes) || !TryAdd(Bytes, LinkBytes, Bytes)
		|| Bytes > M5FingerprintMaxBytes || !Budget->TryChargeWork(VariableItems))
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Source bytes or work exceed limit"));
	}
	// The legacy writer lowercases names and converts them to UTF-8. Restricting
	// this first metered path to ASCII makes its byte length and allocation bound
	// exact and avoids creating temporary lower-case/UTF-8 strings.
	for (const FVoxelLandGraphParameter& P : Source.Parameters)
	{
		for (int32 I = 0; I < P.Name.Len(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			if (P.Name[I] <= 0 || P.Name[I] > 127)
			{
				return M5FingerprintError(
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
		|| !TryAdd(ScratchBytes, 4096, ScratchBytes) || !Budget->TryChargeWork(Work))
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fingerprint work exceeds limit"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(ScratchBytes, Lease))
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Fingerprint memory exceeds limit"));
	}
	TArray<int32> ParameterOrder, NodeOrder, LinkOrder;
	if (!M5FingerprintSortIndices(
			Source.Parameters,
			[](const FVoxelLandGraphParameter& A, const FVoxelLandGraphParameter& B) {
				return M5FingerprintLessGuid(A.Id, B.Id);
			},
			IsCancelled, ParameterOrder)
		|| !M5FingerprintSortIndices(
			Source.Nodes,
			[](const FVoxelLandGraphNodeRecord& A, const FVoxelLandGraphNodeRecord& B) {
				return M5FingerprintLessGuid(A.Id, B.Id);
			},
			IsCancelled, NodeOrder)
		|| !M5FingerprintSortIndices(Source.Links, M5FingerprintLessLink, IsCancelled, LinkOrder))
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	for (int32 I = 1; I < ParameterOrder.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (Source.Parameters[ParameterOrder[I - 1]].Id == Source.Parameters[ParameterOrder[I]].Id)
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate parameter ID"));
		}
	}
	for (int32 I = 1; I < NodeOrder.Num(); ++I)
	{
		if (IsCancelled())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		if (Source.Nodes[NodeOrder[I - 1]].Id == Source.Nodes[NodeOrder[I]].Id)
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate node ID"));
		}
	}
	FCanonicalWriter W;
	W.Bytes.Reserve(int32(Bytes));
	W.U32(1);
	W.U32(uint32(Source.SourceVersion));
	W.U32(uint32(ParameterOrder.Num()));
	for (int32 Index : ParameterOrder)
	{
		if (IsCancelled())
		{
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphParameter& P = Source.Parameters[Index];
		M5FingerprintWriteGuid(W, P.Id);
		W.U32(uint32(P.Name.Len()));
		for (int32 I = 0; I < P.Name.Len(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
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
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphNodeRecord& N = Source.Nodes[Index];
		M5FingerprintWriteGuid(W, N.Id);
		M5FingerprintWriteGuid(W, N.OperatorId);
		W.U32(uint32(N.OperatorVersion));
		W.U32(uint32(N.SeedSalt));
		M5FingerprintWriteGuid(W, N.ParameterId);
		W.U32(uint32(N.Configuration.Num()));
		for (int32 Offset = 0; Offset < N.Configuration.Num(); Offset += 4096)
		{
			if (IsCancelled())
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const int32 Count = FMath::Min(4096, N.Configuration.Num() - Offset);
			W.Bytes.Append(N.Configuration.GetData() + Offset, Count);
		}
		W.U32(uint32(N.Pins.Num()));
		for (int32 I = 0; I < N.Pins.Num(); ++I)
		{
			if ((I & 255) == 0 && IsCancelled())
			{
				return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
			}
			const FVoxelLandGraphPinRecord& Pin = N.Pins[I];
			M5FingerprintWriteGuid(W, Pin.Id);
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
			return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
		}
		const FVoxelLandGraphLinkRecord& Link = Source.Links[Index];
		M5FingerprintWriteGuid(W, Link.SourceNode);
		M5FingerprintWriteGuid(W, Link.SourcePin);
		M5FingerprintWriteGuid(W, Link.TargetNode);
		M5FingerprintWriteGuid(W, Link.TargetPin);
	}
	if (IsCancelled())
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (W.Bytes.Num() != Bytes)
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Unproven, TEXT("Canonical byte count mismatch"));
	}
	const uint64 Result = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
	if (IsCancelled())
	{
		return M5FingerprintError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	OutFingerprint = Result;
	return {};
}
