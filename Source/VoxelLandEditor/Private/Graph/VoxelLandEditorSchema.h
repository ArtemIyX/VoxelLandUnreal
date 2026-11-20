#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "VoxelLandEditorSchema.generated.h"

UCLASS()
class UVoxelLandEditorSchema : public UEdGraphSchema
{
    GENERATED_BODY()
public:
    virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
    virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
    virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
    virtual void TrySetDefaultValue(UEdGraphPin& Pin, const FString& NewDefaultValue, bool bMarkAsModified = true) const override;
    virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
};
