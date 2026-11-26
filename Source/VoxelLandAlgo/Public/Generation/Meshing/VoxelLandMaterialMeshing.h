#pragma once

#include "CoreMinimal.h"

struct FVoxelLandMaterialAppearance;
struct FVoxelLandMeshData;

class VOXELLANDALGO_API FVoxelLandMaterialMeshing final
{
public:
	// Apply once to final render topology. Invalid input and cancellation leave the mesh untouched.
	[[nodiscard]] static bool Pack(
		FVoxelLandMeshData& Mesh, const FVoxelLandMaterialAppearance& Appearance, const FVector3d& LogicalOrigin);
	[[nodiscard]] static bool Pack(FVoxelLandMeshData& Mesh, const FVoxelLandMaterialAppearance& Appearance,
		const FVector3d& LogicalOrigin, TFunctionRef<bool()> IsCancelled);
};
