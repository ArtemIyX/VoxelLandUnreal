#pragma once
#include "EdGraphUtilities.h"

class FVoxelLandGraphPinFactory : public FGraphPanelPinFactory
{
public:
    virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};
