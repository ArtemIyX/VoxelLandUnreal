#pragma once

#include "VoxelLandAssetFactories.h"
#include "VoxelLandSurfaceMaterialFactory.generated.h"

UCLASS()
class UVoxelLandSurfaceMaterialFactory final : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandSurfaceMaterialFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
