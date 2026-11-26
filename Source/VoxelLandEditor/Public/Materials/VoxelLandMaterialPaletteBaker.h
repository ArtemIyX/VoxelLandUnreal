#pragma once

#include "CoreMinimal.h"

class UVoxelLandMaterialPalette;

class VOXELLANDEDITOR_API FVoxelLandMaterialPaletteBaker final
{
public:
	[[nodiscard]] static bool Bake(UVoxelLandMaterialPalette& Palette, FText& OutError);
};
