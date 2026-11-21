#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VoxelLandSampleGraphFactory.generated.h"

UCLASS()
class UVoxelLandSampleGraphFactory : public UFactory
{
    GENERATED_BODY()

public:
    UVoxelLandSampleGraphFactory();
    FText GetDisplayName() const override;
    UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
        UObject* Context, FFeedbackContext* Warn) override;
};
