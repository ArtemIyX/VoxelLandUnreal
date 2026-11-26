#pragma once

#include "CoreMinimal.h"

class UTexture2DArray;
class UVoxelLandMaterialPalette;

enum class EVoxelLandMaterialTextureChannel : uint8;

class FVoxelLandMaterialArrayBaker final
{
public:
	static constexpr int64 BytesPerMiB = 1024 * 1024;
	static constexpr int64 MaxStagedSourceBytes = 384 * BytesPerMiB;

	[[nodiscard]] static bool TryAddSourceBytes(int64 BytesPerSlice, int32 SliceCount, int64& InOutSourceBytes);
	[[nodiscard]] static bool ValidateSourceBudget(const UVoxelLandMaterialPalette& Palette, FText& OutError);

	[[nodiscard]] static UTexture2DArray* Build(
		UVoxelLandMaterialPalette& Palette, EVoxelLandMaterialTextureChannel Channel, FText& OutError);
};
