#pragma once

#include "CoreMinimal.h"

class FVoxelLandMaskGraphProgram;
class IVoxelLandMaterialSignalSource;
struct FVoxelLandMaterialAppearance;
struct FVoxelLandMaterialGraphPoint;
struct FVoxelLandMeshData;

class FVoxelLandMaterialPreviewRaster final
{
public:
	using FPreviewMesh = TPair<FIntVector, TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>>;
	static constexpr int32 Resolution = 128;

	static bool Build(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
		const FVoxelLandMaterialAppearance& Appearance, FGuid Node, TFunctionRef<bool()> IsCancelled,
		TArray<uint8>& Pixels, double& Min, double& Max);
	static bool BuildMask(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
		const FVoxelLandMaskGraphProgram& Program, const FVoxelLandMaterialGraphPoint& Context, FGuid Output,
		TFunctionRef<bool()> IsCancelled, TArray<uint8>& Pixels, double& Min, double& Max);

private:
	static bool BuildRaster(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
		const FVoxelLandMaterialGraphPoint& Context,
		TFunctionRef<bool(const FVoxelLandMaterialGraphPoint&, double&)> Evaluate, TFunctionRef<bool()> IsCancelled,
		bool bAbsoluteGray, TArray<uint8>& Pixels, double& Min, double& Max);
};
