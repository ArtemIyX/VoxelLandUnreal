#include "Graph/Capabilities/VoxelLandWorldCapabilities.h"

#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Operators/VoxelLandOperatorStage.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNodeSpec.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"

class FVoxelLandWorldCapabilitiesInternals final
{
public:
	static constexpr int32 WorldCapabilitiesMaxFunctions = 128;
	static constexpr int32 WorldCapabilitiesMaxNodes = 4096;
	static constexpr int32 WorldCapabilitiesMaxProviders = 256;
	static constexpr int32 WorldCapabilitiesMaxPath = 2048;

	struct FNodeValidation
	{
		const FVoxelLandGraphSnapshot& Source;
		TConstArrayView<FVoxelLandWorldOperatorCapability> ProviderCapabilities;
		EVoxelLandWorldKind Kind;
		TFunctionRef<bool()> IsCancelled;
		TArray<uint8>& FunctionEdges;
		TArray<uint8>& Roots;
		FString& Path;
	};

	static FVoxelLandWorldCapabilityDiagnostic ValidateDeclarations(const FVoxelLandGraphSnapshot& Source,
		TConstArrayView<FVoxelLandWorldOperatorCapability> ProviderCapabilities, TFunctionRef<bool()> IsCancelled);
	static FVoxelLandWorldCapabilityDiagnostic ValidateNodes(const TArray<FVoxelLandGraphNodeRecord>& Nodes,
		int32 Parent, const FString& Owner, const FNodeValidation& Context);
	static FVoxelLandWorldCapabilityDiagnostic ValidateFunctionClosure(
		const TArray<uint8>& FunctionEdges, const TArray<uint8>& Roots, TFunctionRef<bool()> IsCancelled);

private:
	static FVoxelLandWorldCapabilityDiagnostic RecordFunctionCall(
		const FVoxelLandGraphNodeRecord& Node, int32 Parent, const FString& Owner, const FNodeValidation& Context);
	static FVoxelLandWorldCapabilityDiagnostic ValidateOperatorRequirement(
		const FVoxelLandGraphNodeRecord& Node, const FString& Owner, const FNodeValidation& Context);

public:
	static FVoxelLandWorldCapabilityDiagnostic WorldCapabilitiesFailure(EVoxelLandWorldCapabilityError Code,
		const TCHAR* Message, const FGuid& NodeId = FGuid(), const FString& FunctionPath = FString())
	{
		FVoxelLandWorldCapabilityDiagnostic Result;
		Result.Code = Code;
		Result.NodeId = NodeId;
		Result.FunctionPath = FunctionPath;
		Result.Message = Message;
		return Result;
	}

	static bool WorldCapabilitiesValidRequirement(EVoxelLandWorldRequirement Requirement)
	{
		switch (Requirement)
		{
			case EVoxelLandWorldRequirement::SampleLocal:
			case EVoxelLandWorldRequirement::BoundedNeighborhood:
			case EVoxelLandWorldRequirement::BoundedRegion:
			case EVoxelLandWorldRequirement::FiniteGlobal:
				return true;
			default:
				return false;
		}
	}

	// Check the bounded lengths of the stored native path components before ToString
	// can allocate. No referenced UObject is loaded or dereferenced on the worker.
	static bool WorldCapabilitiesPath(const FSoftObjectPath& Path, FString& Out)
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
		if (Upper > WorldCapabilitiesMaxPath)
		{
			return false;
		}
		Out = Path.ToString();
		return !Out.IsEmpty() && Out.Len() <= WorldCapabilitiesMaxPath;
	}
};

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilitiesInternals::ValidateDeclarations(
	const FVoxelLandGraphSnapshot& Source, TConstArrayView<FVoxelLandWorldOperatorCapability> ProviderCapabilities,
	TFunctionRef<bool()> IsCancelled)
{
	const int32 FunctionCount = Source.Functions.Num();
	for (int32 I = 0; I < ProviderCapabilities.Num(); ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
		}
		const FVoxelLandWorldOperatorCapability& A = ProviderCapabilities[I];
		if (!A.OperatorId.IsValid() || A.OperatorVersion < 1
			|| !FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesValidRequirement(A.Requirement))
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Invalid provider capability"));
		}
		for (int32 J = 0; J < I; ++J)
		{
			if (IsCancelled())
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
			}
			const FVoxelLandWorldOperatorCapability& B = ProviderCapabilities[J];
			if (A.OperatorId == B.OperatorId && A.OperatorVersion == B.OperatorVersion)
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Duplicate provider capability"));
			}
		}
	}
	for (int32 I = 0; I < FunctionCount; ++I)
	{
		for (int32 J = 0; J < I; ++J)
		{
			if (IsCancelled())
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
			}
			if (Source.Functions[I].AssetPath == Source.Functions[J].AssetPath)
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Duplicate captured function path"));
			}
		}
	}
	return {};
}

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilitiesInternals::RecordFunctionCall(
	const FVoxelLandGraphNodeRecord& Node, int32 Parent, const FString& Owner, const FNodeValidation& Context)
{
	const int32 FunctionCount = Context.Source.Functions.Num();
	int32 Target = INDEX_NONE;
	for (int32 I = 0; I < FunctionCount; ++I)
	{
		if (Context.IsCancelled())
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"), Node.Id,
				Owner);
		}
		if (Context.Source.Functions[I].AssetPath == Context.Path)
		{
			Target = I;
			break;
		}
	}
	if (Target == INDEX_NONE)
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Missing captured function dependency"), Node.Id, Owner);
	}
	if (Parent == INDEX_NONE)
	{
		Context.Roots[Target] = 1;
	}
	else
	{
		Context.FunctionEdges[Parent * FunctionCount + Target] = 1;
	}
	return {};
}

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilitiesInternals::ValidateOperatorRequirement(
	const FVoxelLandGraphNodeRecord& Node, const FString& Owner, const FNodeValidation& Context)
{
	EVoxelLandWorldRequirement Requirement = EVoxelLandWorldRequirement::SampleLocal;
	bool bKnown = false;
	if (Node.OperatorId.A == 0x564c4d33 && Node.OperatorId.B == 1 && Node.OperatorId.C == 0)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		// The current built-in list is explicitly classified here. A future
		// built-in must not become infinite-safe merely by using Sample stage.
		if (Node.OperatorId.D <= uint32(EVoxelLandBuiltinNode::M4HeightOutput)
			&& FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(Node.OperatorId.D), Spec)
			&& Node.OperatorId == Spec.OperatorId && Node.OperatorVersion == Spec.OperatorVersion)
		{
			if (Spec.Stage == EVoxelLandOperatorStage::Sample)
			{
				Requirement = EVoxelLandWorldRequirement::SampleLocal;
				bKnown = true;
			}
			else if (Spec.Stage == EVoxelLandOperatorStage::OneCellFilter)
			{
				Requirement = EVoxelLandWorldRequirement::BoundedNeighborhood;
				bKnown = true;
			}
		}
	}
	else
	{
		for (const FVoxelLandWorldOperatorCapability& Capability : Context.ProviderCapabilities)
		{
			if (Context.IsCancelled())
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"), Node.Id,
					Owner);
			}
			if (Capability.OperatorId == Node.OperatorId && Capability.OperatorVersion == Node.OperatorVersion)
			{
				Requirement = Capability.Requirement;
				bKnown = true;
				break;
			}
		}
	}
	if (!bKnown)
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::UnknownCapability, TEXT("Operator has no known world requirement"), Node.Id,
			Owner);
	}
	if (Context.Kind == EVoxelLandWorldKind::Infinite && Requirement == EVoxelLandWorldRequirement::FiniteGlobal)
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::FiniteWorldRequired, TEXT("Operator requires a complete finite world"),
			Node.Id, Owner);
	}
	return {};
}

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilitiesInternals::ValidateNodes(
	const TArray<FVoxelLandGraphNodeRecord>& Nodes, int32 Parent, const FString& Owner, const FNodeValidation& Context)
{
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FVoxelLandGraphNodeRecord& Node = Nodes[NodeIndex];
		if (Context.IsCancelled())
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"), Node.Id,
				Owner);
		}
		if (!Node.Id.IsValid() || !Node.OperatorId.IsValid() || Node.OperatorVersion < 1)
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Invalid node or operator identifier"), Node.Id,
				Owner);
		}
		for (int32 Prior = 0; Prior < NodeIndex; ++Prior)
		{
			if (Context.IsCancelled())
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"), Node.Id,
					Owner);
			}
			if (Nodes[Prior].Id == Node.Id)
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Duplicate node identifier"), Node.Id, Owner);
			}
		}
		const bool bCall = Node.OperatorId == VoxelLandGraphFunctionCallId();
		if (bCall != !Node.FunctionPath.IsNull()
			|| !FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesPath(Node.FunctionPath, Context.Path))
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Invalid function call path"), Node.Id, Owner);
		}
		const FVoxelLandWorldCapabilityDiagnostic Diagnostic = bCall
																 ? RecordFunctionCall(Node, Parent, Owner, Context)
																 : ValidateOperatorRequirement(Node, Owner, Context);
		if (Diagnostic.Code != EVoxelLandWorldCapabilityError::None)
		{
			return Diagnostic;
		}
	}
	return {};
}

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilitiesInternals::ValidateFunctionClosure(
	const TArray<uint8>& FunctionEdges, const TArray<uint8>& Roots, TFunctionRef<bool()> IsCancelled)
{
	const int32 FunctionCount = Roots.Num();
	// Validate every dependency is reachable and reject cycles. Since counts are
	// bounded, transitive closure is deterministic and independent of task order.
	TArray<uint8> Closure = FunctionEdges;
	for (int32 K = 0; K < FunctionCount; ++K)
	{
		for (int32 I = 0; I < FunctionCount; ++I)
		{
			for (int32 J = 0; J < FunctionCount; ++J)
			{
				if (IsCancelled())
				{
					return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
						EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
				}
				Closure[I * FunctionCount + J] |= Closure[I * FunctionCount + K] && Closure[K * FunctionCount + J];
			}
		}
	}
	for (int32 I = 0; I < FunctionCount; ++I)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
		}
		if (Closure[I * FunctionCount + I])
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Cyclic captured function dependency"));
		}
		bool bReachable = Roots[I] != 0;
		for (int32 J = 0; J < FunctionCount && !bReachable; ++J)
		{
			if (IsCancelled())
			{
				return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
					EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
			}
			bReachable = Roots[J] && Closure[J * FunctionCount + I];
		}
		if (!bReachable)
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Unreferenced captured function dependency"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
	}
	return {};
}

FVoxelLandWorldCapabilityDiagnostic FVoxelLandWorldCapabilities::Validate(const FVoxelLandGraphSnapshot& Source,
	EVoxelLandWorldKind Kind, TConstArrayView<FVoxelLandWorldOperatorCapability> ProviderCapabilities,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
	}
	if ((Kind != EVoxelLandWorldKind::Finite && Kind != EVoxelLandWorldKind::Infinite)
		|| Source.Nodes.Num() > FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxNodes
		|| Source.Functions.Num() > FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxFunctions
		|| ProviderCapabilities.Num() > FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxProviders)
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Invalid world kind or capability input limits"));
	}
	const int32 FunctionCount = Source.Functions.Num();
	if (!Budget->TryChargeWork(int64(FunctionCount) * 4 + 1))
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World capability preflight work exceeds budget"));
	}
	int64 TotalNodes = Source.Nodes.Num();
	for (const FVoxelLandGraphFunctionSnapshot& Function : Source.Functions)
	{
		if (IsCancelled())
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::Cancelled, TEXT("World capability validation cancelled"));
		}
		if (Function.AssetPath.IsEmpty()
			|| Function.AssetPath.Len() > FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxPath
			|| Function.Nodes.Num() > FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxNodes - TotalNodes)
		{
			return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
				EVoxelLandWorldCapabilityError::InvalidInput, TEXT("Invalid or oversized captured function"));
		}
		TotalNodes += Function.Nodes.Num();
	}
	// All factors are bounded by the constants above; int64 is sufficient.
	// Charge worst-case path-character comparisons, node/provider scans and
	// dependency closure before allocating scratch or invoking comparisons.
	const int64 Functions = FunctionCount;
	const int64 Providers = ProviderCapabilities.Num();
	const int64 Work =
		4096
		+ TotalNodes
			  * (Functions * (FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxPath + 4) + Providers * 8 + 32)
		+ TotalNodes * TotalNodes * 4
		+ Functions * Functions * (FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesMaxPath + Functions * 4 + 16)
		+ Providers * Providers * 8;
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World capability work exceeds budget"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	const int64 Bytes = 32768 + 2 * Functions * Functions + 4 * Functions;
	if (!Budget->TryReserveBytes(Bytes, ScratchLease))
	{
		return FVoxelLandWorldCapabilitiesInternals::WorldCapabilitiesFailure(
			EVoxelLandWorldCapabilityError::BudgetExceeded, TEXT("World capability scratch exceeds budget"));
	}
	using FValidation = FVoxelLandWorldCapabilitiesInternals;
	FVoxelLandWorldCapabilityDiagnostic Diagnostic =
		FValidation::ValidateDeclarations(Source, ProviderCapabilities, IsCancelled);
	if (Diagnostic.Code != EVoxelLandWorldCapabilityError::None)
	{
		return Diagnostic;
	}
	TArray<uint8> FunctionEdges;
	FunctionEdges.Init(0, FunctionCount * FunctionCount);
	TArray<uint8> Roots;
	Roots.Init(0, FunctionCount);
	FString Path;
	const FValidation::FNodeValidation Context{
		Source, ProviderCapabilities, Kind, IsCancelled, FunctionEdges, Roots, Path};
	Diagnostic = FValidation::ValidateNodes(Source.Nodes, INDEX_NONE, FString(), Context);
	if (Diagnostic.Code != EVoxelLandWorldCapabilityError::None)
	{
		return Diagnostic;
	}
	for (int32 I = 0; I < FunctionCount; ++I)
	{
		Diagnostic = FValidation::ValidateNodes(Source.Functions[I].Nodes, I, Source.Functions[I].AssetPath, Context);
		if (Diagnostic.Code != EVoxelLandWorldCapabilityError::None)
		{
			return Diagnostic;
		}
	}
	return FValidation::ValidateFunctionClosure(FunctionEdges, Roots, IsCancelled);
}
