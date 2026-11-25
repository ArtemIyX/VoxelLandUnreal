#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"

// Editor-only text and style. Serialized operator contracts live in VoxelLandData.
class FVoxelLandBuiltinPresentationCatalog
{
public:
	static bool Validate(FString& OutError);
	static FText Title(EVoxelLandBuiltinNode Kind);
	static FText Description(EVoxelLandBuiltinNode Kind);
	static FText Category(EVoxelLandBuiltinNode Kind);
	static FLinearColor TitleColor(EVoxelLandBuiltinNode Kind);
	// Returns false when the graph node's standard pin label should be used.
	static bool TryPinLabel(EVoxelLandBuiltinNode Kind, FName PinName, FText& OutLabel);
	static FText PinTooltip(EVoxelLandBuiltinNode Kind, FName PinName, FName PinType, bool bOutput);

private:
	static FText ExplicitTitle(EVoxelLandBuiltinNode Kind);
	static FText ExplicitDescription(EVoxelLandBuiltinNode Kind);
	static FText ExplicitCategory(EVoxelLandBuiltinNode Kind);
	static FLinearColor ExplicitTitleColor(EVoxelLandBuiltinNode Kind);
};