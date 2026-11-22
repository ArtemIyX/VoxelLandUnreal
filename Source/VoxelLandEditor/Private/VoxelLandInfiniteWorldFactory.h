#pragma once

#include "Factories/Factory.h"
#include "VoxelLandInfiniteWorldFactory.generated.h"

UCLASS()
class UVoxelLandInfiniteWorldFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVoxelLandInfiniteWorldFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
