#pragma once

#include "VoxelLandAssetFactories.h"
#include "VoxelLandMaskGraphFactory.generated.h"

UCLASS()
class UVoxelLandMaskGraphFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandMaskGraphFactory();
	FText GetDisplayName() const override;
	UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context,
		FFeedbackContext* Warn) override;
};
