#pragma once

#include "Factories/Factory.h"
#include "VoxelLandFiniteWorldFactory.generated.h"

UCLASS()
class UVoxelLandFiniteWorldFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandFiniteWorldFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
