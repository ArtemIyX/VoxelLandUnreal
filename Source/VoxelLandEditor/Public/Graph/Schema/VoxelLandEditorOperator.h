#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Sources/VoxelLandGraphPinRecord.h"

// Editor-only, value-owned presentation for an already registered runtime operator.
// Providers should unregister their descriptor when their editor module shuts down.
class VOXELLANDEDITOR_API FVoxelLandEditorOperatorPresentation
{
public:
	FGuid OperatorId;
	int32 Version = 1;
	FText Category;
	FText DisplayName;
	FText Tooltip;
	TArray<FText> InputTooltips;
	FText OutputTooltip;
	FLinearColor NodeColor = FLinearColor(0.35f, 0.24f, 0.45f);
	TArray<FVoxelLandGraphPinRecord> Inputs;
	FString OutputName = TEXT("Result");
	EVoxelLandGraphValueType OutputType = EVoxelLandGraphValueType::Float;
	TArray<uint8> DefaultConfiguration;
};

// Registration and enumeration are game-thread only. Copies contain no provider
// callbacks or module pointers; executable behavior remains in the runtime registry.
class VOXELLANDEDITOR_API FVoxelLandEditorOperatorRegistry
{
public:
	static bool Register(const FVoxelLandEditorOperatorPresentation& Presentation, FString& OutError);
	static bool Unregister(const FGuid& OperatorId, int32 Version);
	static bool FindPresentation(
		const FGuid& OperatorId, int32 Version, FVoxelLandEditorOperatorPresentation& OutPresentation);
	static void GetPresentations(TArray<FVoxelLandEditorOperatorPresentation>& OutPresentations);
	static uint64 GetRevision();
	static void NotifyFunctionChanged();
	static void Clear();
};