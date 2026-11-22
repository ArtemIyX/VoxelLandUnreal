#include "Data/VoxelLandGraphFunctionCapture.h"

#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Structs/VoxelLandGraphDiagnostic.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/VoxelLandGraphFunctionId.h"

namespace
{
constexpr int32 MaxDepth = 16;
constexpr int32 MaxFunctions = 128;
constexpr int64 MaxCapturedNodes = 4096;
constexpr int64 MaxCapturedLinks = 16384;
constexpr int64 MaxConfigBytes = 1048576;

bool CheckNodeBudget(const TArray<FVoxelLandGraphNodeRecord>& Nodes, int64& TotalConfig,
	FVoxelLandGraphDiagnostic& Diagnostic)
{
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		TotalConfig += Node.Configuration.Num();
		if (Node.Pins.Num() > 64 || Node.Configuration.Num() > 65536 || TotalConfig > MaxConfigBytes ||
		    Node.FunctionPath.ToString().Len() > 2048)
		{
			Diagnostic.NodeId = Node.Id;
			Diagnostic.Message = TEXT("Function graph contains oversized pins, configuration or path.");
			return false;
		}
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (Pin.Name.Len() > 128)
			{
				Diagnostic.NodeId = Node.Id;
				Diagnostic.Message = TEXT("Function graph contains an oversized pin name.");
				return false;
			}
		}
	}
	return true;
}

class FFunctionCapture
{
public:
	FFunctionCapture(FVoxelLandGraphSnapshot& InSnapshot, FVoxelLandGraphDiagnostic& InDiagnostic)
		: Snapshot(InSnapshot), Diagnostic(InDiagnostic) {}

	bool Run()
	{
		TotalNodes = Snapshot.Nodes.Num();
		TotalLinks = Snapshot.Links.Num();
		if (!CheckNodeBudget(Snapshot.Nodes, TotalConfig, Diagnostic))
		{
			return false;
		}
		int32 Height = 0;
		return VisitNodes(Snapshot.Nodes, 0, Height);
	}

private:
	bool VisitNodes(TArray<FVoxelLandGraphNodeRecord>& Nodes, int32 Depth, int32& OutHeight)
	{
		OutHeight = 0;
		for (FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			const FGuid RootId = CurrentRootCall.IsValid() ? CurrentRootCall : Node.Id;
			TGuardValue<FGuid> RootGuard(CurrentRootCall, RootId);
			if (Node.OperatorId != VoxelLandGraphFunctionCallId())
			{
				if (!Node.FunctionPath.IsNull())
				{
					return Fail(Node, TEXT("Only a function call may contain a function path."));
				}
				continue;
			}
			if (Node.FunctionPath.IsNull())
			{
				return Fail(Node, TEXT("Function call has no function asset."));
			}
			UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(Node.FunctionPath.TryLoad());
			if (!Function)
			{
				return Fail(Node, TEXT("Function asset is missing or has the wrong class."));
			}
			Node.FunctionPath = FSoftObjectPath(Function);
			int32 Height = 0;
			if (!VisitFunction(*Function, Node, Depth + 1, Height))
			{
				return false;
			}
			OutHeight = FMath::Max(OutHeight, Height);
		}
		return true;
	}

	bool VisitFunction(const UVoxelLandGraphFunction& Function, const FVoxelLandGraphNodeRecord& Call, int32 Depth,
		int32& OutHeight)
	{
		const FString Key = Function.GetPathName();
		if (Depth > MaxDepth)
		{
			return Fail(Call, TEXT("Function nesting exceeds its limit."));
		}
		if (Visiting.Contains(Key))
		{
			return Fail(Call, TEXT("Recursive function dependency detected."));
		}
		if (const int32* Height = Heights.Find(Key))
		{
			OutHeight = *Height;
			if (Depth + *Height - 1 > MaxDepth)
			{
				return Fail(Call, TEXT("Function nesting exceeds its limit."));
			}
			return true;
		}
		if (Heights.Num() + Visiting.Num() >= MaxFunctions || Function.SourceVersion != 1)
		{
			return Fail(Call, TEXT("Function nesting, count, or source version is unsupported."));
		}
		TotalNodes += Function.Nodes.Num();
		TotalLinks += Function.Links.Num();
		if (TotalNodes > MaxCapturedNodes || TotalLinks > MaxCapturedLinks || Function.Parameters.Num() > 63 ||
		    !CheckNodeBudget(Function.Nodes, TotalConfig, Diagnostic))
		{
			return Fail(Call, TEXT("Function dependencies exceed capture limits."));
		}
		for (const FVoxelLandGraphParameter& Input : Function.Parameters)
		{
			if (Input.Name.Len() > 128 || Input.Tooltip.ToString().Len() > 4096)
			{
				return Fail(Call, TEXT("Function interface text exceeds capture limits."));
			}
		}
		FVoxelLandGraphFunctionSnapshot Copy;
		Copy.AssetPath = Key;
		Copy.SourceVersion = Function.SourceVersion;
		Copy.Inputs = Function.Parameters;
		Copy.OutputType = Function.OutputType;
		Copy.OutputNode = Function.OutputNode;
		Copy.OutputPin = Function.OutputPin;
		Copy.Nodes = Function.Nodes;
		Copy.Links = Function.Links;
		Visiting.Add(Key);
		int32 ChildHeight = 0;
		TGuardValue<FString> FunctionGuard(CurrentFunction, Key);
		if (!VisitNodes(Copy.Nodes, Depth, ChildHeight))
		{
			return false;
		}
		Visiting.Remove(Key);
		OutHeight = 1 + ChildHeight;
		Heights.Add(Key, OutHeight);
		Snapshot.Functions.Add(MoveTemp(Copy));
		return true;
	}

	bool Fail(const FVoxelLandGraphNodeRecord& Node, const TCHAR* Message)
	{
		Diagnostic.Message = CurrentFunction.IsEmpty()
			                     ? FString::Printf(TEXT("%s Node %s."), Message, *Node.Id.ToString())
			                     : FString::Printf(TEXT("%s Function %s, node %s."), Message, *CurrentFunction,
				                     *Node.Id.ToString());
		Diagnostic.NodeId = CurrentRootCall.IsValid() ? CurrentRootCall : Node.Id;
		return false;
	}

	FVoxelLandGraphSnapshot& Snapshot;
	FVoxelLandGraphDiagnostic& Diagnostic;
	TSet<FString> Visiting;
	TMap<FString, int32> Heights;
	FGuid CurrentRootCall;
	FString CurrentFunction;
	int64 TotalConfig = 0;
	int64 TotalNodes = 0;
	int64 TotalLinks = 0;
};
}

bool FVoxelLandGraphFunctionCapture::Capture(FVoxelLandGraphSnapshot& Snapshot, FVoxelLandGraphDiagnostic& Diagnostic)
{
	Diagnostic = FVoxelLandGraphDiagnostic();
	if (!IsInGameThread())
	{
		Diagnostic.Message = TEXT("Function dependencies must be captured on the game thread.");
		return false;
	}
	const bool bHasCalls = Snapshot.Nodes.ContainsByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
		return Node.OperatorId == VoxelLandGraphFunctionCallId() || !Node.FunctionPath.IsNull();
	});
	if (!bHasCalls)
	{
		Snapshot.Functions.Reset();
		return true;
	}
	int64 RootConfig = 0;
	if (Snapshot.Nodes.Num() > MaxCapturedNodes || Snapshot.Links.Num() > MaxCapturedLinks ||
	    !CheckNodeBudget(Snapshot.Nodes, RootConfig, Diagnostic))
	{
		if (Diagnostic.Message.IsEmpty()) { Diagnostic.Message = TEXT("Root graph exceeds function capture limits."); }
		return false;
	}
	FVoxelLandGraphSnapshot Copy = Snapshot;
	Copy.Functions.Reset();
	if (!FFunctionCapture(Copy, Diagnostic).Run())
	{
		return false;
	}
	Snapshot = MoveTemp(Copy);
	return true;
}