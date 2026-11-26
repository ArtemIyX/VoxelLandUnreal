#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

struct FVoxelLandMaterialAppearance;

struct FVoxelLandTerrainPresentation
{
	TStrongObjectPtr<UMaterialInterface> Material;
	TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance;
	FGuid LayoutIdentity;
	TArray<uint8> SourceIdentity;
	FString Diagnostic;
	bool bHasConfiguredPalette = false;
};
