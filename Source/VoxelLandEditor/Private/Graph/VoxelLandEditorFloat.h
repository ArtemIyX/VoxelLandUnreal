#pragma once
#include "CoreMinimal.h"

class FVoxelLandEditorFloat
{
public:
    static FString Format(float Value)
    {
        return FString::Printf(TEXT("%.9g"), double(Value));
    }
};
