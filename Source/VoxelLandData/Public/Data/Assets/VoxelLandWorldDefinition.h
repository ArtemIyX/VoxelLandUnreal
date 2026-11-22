#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Enums/VoxelLandWorldKind.h"
#include "Data/Structs/VoxelLandWorldCommonSettings.h"
#include "Templates/Function.h"
#include "VoxelLandWorldDefinition.generated.h"

struct FVoxelLandWorldCommonSnapshot;

#if WITH_DEV_AUTOMATION_TESTS
// Test-only hook to model reentrancy immediately before each function load; clear after use.
VOXELLANDDATA_API void VoxelLandWorldDefinitionSetBeforeLoadHook(TFunction<void()>&& Hook);
#endif

// Abstract common authoring data. A concrete asset type determines its world
// mode; there is no separately editable flag that can contradict the type.
UCLASS(Abstract, BlueprintType)
class VOXELLANDDATA_API UVoxelLandWorldDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	virtual EVoxelLandWorldKind GetWorldKind() const
		PURE_VIRTUAL(UVoxelLandWorldDefinition::GetWorldKind, return EVoxelLandWorldKind::Finite;);

	UPROPERTY(EditAnywhere, Category = "VoxelLand",
		meta = (ToolTip = "Terrain inputs shared by finite and infinite-style worlds."))
	FVoxelLandWorldCommonSettings Common;

protected:
	// Game thread only; leaves OutSnapshot unchanged on failure. This captures
	// source inputs, not proof that a graph is valid for the selected world mode.
	bool CaptureCommon(EVoxelLandWorldKind Kind, FVoxelLandWorldCommonSnapshot& OutSnapshot, FString& OutError) const;
};
