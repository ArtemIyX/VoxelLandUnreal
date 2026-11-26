#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VoxelLandAssetFactories.generated.h"

uint32 GetVoxelLandAssetMenuCategory();

UCLASS(Abstract)
class UVoxelLandAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	virtual uint32 GetMenuCategories() const override;
};

UCLASS()
class UVoxelLandTerrainGraphFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandTerrainGraphFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandGraphFunctionFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandGraphFunctionFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandWorldConfigFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandWorldConfigFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandMaterialSetupFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandMaterialSetupFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandCaveProtectionFactory : public UVoxelLandAssetFactory
{
	GENERATED_BODY()

public:
	UVoxelLandCaveProtectionFactory();
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
