#pragma once

#include "PCGElement.h"

class UPCGVoxelTerrainData;
class UVoxelLandPCGVoxelProjectSettings;
struct FPCGTaggedData;
struct FVoxelLandTerrainPlacementMesh;

class FVoxelLandPCGVoxelProjectElement final : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
	static bool ProjectInput(FPCGContext& Context, const FPCGTaggedData& Tagged, const UPCGVoxelTerrainData& Data,
		TConstArrayView<FVoxelLandTerrainPlacementMesh> Meshes, const FVector& Direction, const FVector& Origin,
		const UVoxelLandPCGVoxelProjectSettings& Settings, int32 PointBudget, int32& ProcessedPoints);
};
