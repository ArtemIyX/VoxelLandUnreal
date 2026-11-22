#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VoxelLandAssetFactories.generated.h"

UCLASS()
class UVoxelLandTerrainGraphFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandTerrainGraphFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandM4ControlGraphFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVoxelLandM4ControlGraphFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandGraphFunctionFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandGraphFunctionFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandWorldSettingsFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandWorldSettingsFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandM4WorldConfigFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandM4WorldConfigFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class UVoxelLandMaterialSetupFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandMaterialSetupFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
