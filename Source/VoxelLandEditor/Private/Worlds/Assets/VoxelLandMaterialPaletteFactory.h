#pragma once

#include "VoxelLandAssetFactories.h"
#include "VoxelLandMaterialPaletteFactory.generated.h"

UCLASS()
class UVoxelLandMaterialPaletteFactory final : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandMaterialPaletteFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
