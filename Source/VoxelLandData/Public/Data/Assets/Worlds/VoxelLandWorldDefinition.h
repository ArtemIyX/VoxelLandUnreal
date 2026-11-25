#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/World/Configuration/VoxelLandWorldKind.h"
#include "Data/World/Configuration/VoxelLandWorldCommonSettings.h"
#include "Templates/Function.h"
#include "VoxelLandWorldDefinition.generated.h"

struct FVoxelLandWorldCommonSnapshot;
struct FVoxelLandWorldSourceSnapshot;

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
	virtual void Serialize(FArchive& Ar) override;
	// Captures the concrete asset's mode and inputs together on the game thread.
	TSharedPtr<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> CaptureSource(FString& OutError) const;
	const TCHAR* GetPreparationBlocker() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	// A world definition is not cookable until its complete cooked generation
	// and reconstruction path is implemented. Editor saves remain available.
	void EnforceCookPreparation(FArchive& Ar) const;
#endif
	virtual EVoxelLandWorldKind GetWorldKind() const
		PURE_VIRTUAL(UVoxelLandWorldDefinition::GetWorldKind, return EVoxelLandWorldKind::Finite;);

	UPROPERTY(EditAnywhere, Category = "VoxelLand",
		meta = (ToolTip = "Terrain inputs shared by finite and infinite-style worlds."))
	FVoxelLandWorldCommonSettings Common;

protected:
	static bool HasRepresentableVerticalExtent(int64 Minimum, int64 Maximum);

	// Game thread only; leaves OutSnapshot unchanged on failure. This captures
	// source inputs, not proof that a graph is valid for the selected world mode.
	bool CaptureCommon(EVoxelLandWorldKind Kind, TFunctionRef<bool()> IsModeCurrent,
		FVoxelLandWorldCommonSnapshot& OutSnapshot, FString& OutError) const;
};
