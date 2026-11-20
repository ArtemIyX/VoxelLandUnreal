#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Data/Structs/VoxelLandGraphPinRecord.h"
#include "VoxelLandEditorNode.generated.h"

UCLASS()
class UVoxelLandEditorNode : public UEdGraphNode
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category="VoxelLand")
    int32 Kind = 1;

    UPROPERTY(EditAnywhere, Category="VoxelLand")
    FGuid ParameterId;

    UPROPERTY()
    FGuid ExternalOperatorId;

    UPROPERTY()
    int32 OperatorVersion = 1;

    UPROPERTY()
    TArray<uint8> Configuration;

    UPROPERTY()
    TArray<FVoxelLandGraphPinRecord> ExternalPins;

    UPROPERTY()
    FText ExternalTitle;

    UPROPERTY(EditAnywhere, Category="VoxelLand")
    int32 SeedSalt = 0;

    UPROPERTY()
    int32 VariadicInputs = 2;

    virtual void AllocateDefaultPins() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
    virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
    virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
    void AddVariadicInput();
    void RemoveVariadicInput(FGuid PinId);
    void MoveVariadicInput(FGuid PinId, int32 Direction);
    void RenumberVariadicInputs();
    static FText DisplayName(int32 InKind);
};
