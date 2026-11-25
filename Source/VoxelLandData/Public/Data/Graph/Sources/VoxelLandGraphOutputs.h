#pragma once

#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"

class VOXELLANDDATA_API FVoxelLandGraphOutputs
{
public:
	static bool IsNamedOutput(const FGuid& Operator);
	static FString GetName(const FVoxelLandGraphNodeRecord& Node);
	static FString GetName(const FVoxelLandGraphNodeRecord& Node, const FGuid& SelectedOutput);
	static void SetName(FVoxelLandGraphNodeRecord& Node, const FString& Name);
	static bool HasNamedOutputs(TConstArrayView<FVoxelLandGraphNodeRecord> Nodes);

	// Empty selection resolves the legacy Density Output only. Named outputs require an explicit binding.
	static bool Project(FVoxelLandGraphSnapshot& Snapshot, const FGuid& Output, bool bForPreview, FString& Error);
	static bool ProjectPin(FVoxelLandGraphSnapshot& Snapshot, const FGuid& Node, const FGuid& Pin, FString& Error);
};
