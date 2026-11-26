#pragma once

#include "CoreMinimal.h"

struct FVoxelLandMaterialAppearance;
struct FVoxelLandMeshData;

class FVoxelLandMaterialPreviewRaster final
{
public:
	using FPreviewMesh = TPair<FIntVector, TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>>;
	static constexpr int32 Resolution = 128;

	static bool Build(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
		const FVoxelLandMaterialAppearance& Appearance, FGuid Node, TFunctionRef<bool()> IsCancelled,
		TArray<uint8>& Pixels, double& Min, double& Max);
};
