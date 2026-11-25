#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "VoxelLandEditorSchema.generated.h"

class UVoxelLandM4ControlGraph;

UCLASS()
class UVoxelLandEditorSchema : public UEdGraphSchema
{
	GENERATED_BODY()
public:
	// Assigns a legacy M4 output transactionally and schedules normal graph compilation.
	static bool AssignLegacyM4Output(UVoxelLandM4ControlGraph& Graph, UEdGraph& EditorGraph, FGuid NodeId, int32 Slot);
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual void TrySetDefaultValue(
		UEdGraphPin& Pin, const FString& NewDefaultValue, bool bMarkAsModified = true) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
};
