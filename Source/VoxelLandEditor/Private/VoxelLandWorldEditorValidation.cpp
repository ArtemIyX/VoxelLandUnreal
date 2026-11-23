#include "VoxelLandWorldEditorValidation.h"

#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandBuiltinNodeSpec.h"
#include "Data/Structs/VoxelLandGraphFunctionSnapshot.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandWorldCommonSnapshot.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "World/VoxelLandWorldValidation.h"

bool VoxelLandWorldEditorValidation::CanValidateSynchronously(
	const FVoxelLandGraphSnapshot& Snapshot, FString& OutError)
{
	const auto CheckNodes = [&OutError](const TArray<FVoxelLandGraphNodeRecord>& Nodes) {
		for (const FVoxelLandGraphNodeRecord& Node : Nodes)
		{
			if (Node.OperatorId == VoxelLandGraphFunctionCallId())
			{
				continue;
			}
			FVoxelLandBuiltinNodeSpec Spec;
			if (Node.OperatorId.A != 0x564c4d33 || Node.OperatorId.B != 1 || Node.OperatorId.C != 0
				|| Node.OperatorId.D > uint32(EVoxelLandBuiltinNode::M4HeightOutput)
				|| !FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(Node.OperatorId.D), Spec)
				|| Node.OperatorId != Spec.OperatorId || Node.OperatorVersion != Spec.OperatorVersion)
			{
				OutError = TEXT("Graphs with external or unknown operators cannot be validated during synchronous "
								"save. Provider validation is deferred until asynchronous validation is available.");
				return false;
			}
		}
		return true;
	};
	if (!CheckNodes(Snapshot.Nodes))
	{
		return false;
	}
	for (const FVoxelLandGraphFunctionSnapshot& Function : Snapshot.Functions)
	{
		if (!CheckNodes(Function.Nodes))
		{
			return false;
		}
	}
	OutError.Reset();
	return true;
}

bool VoxelLandWorldEditorValidation::CanValidateSynchronously(
	const FVoxelLandWorldCommonSnapshot& Snapshot, FString& OutError)
{
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	for (const FVoxelLandM4ControlSnapshot* Channel : Channels)
	{
		if (!CanValidateSynchronously(Channel->Source, OutError))
		{
			return false;
		}
	}
	return true;
}

bool VoxelLandWorldEditorValidation::ValidateSnapshot(const FVoxelLandWorldCommonSnapshot& Snapshot,
	EVoxelLandWorldKind Kind, const FVoxelLandOperatorRegistrySnapshot& Providers,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled, FString& OutError)
{
	return VoxelLandWorldValidation::ValidateSnapshot(Snapshot, Kind, Providers, Budget, IsCancelled, OutError);
}
