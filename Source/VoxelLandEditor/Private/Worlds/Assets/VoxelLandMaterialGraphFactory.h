#pragma once

#include "VoxelLandAssetFactories.h"
#include "VoxelLandMaterialGraphFactory.generated.h"

UCLASS()
class UVoxelLandMaterialGraphFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandMaterialGraphFactory();
	FText GetDisplayName() const override;
	UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context,
		FFeedbackContext* Warn) override;
};
