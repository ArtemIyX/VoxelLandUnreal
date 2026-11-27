#pragma once

#include "VoxelLandPCGVoxelSurfaceSamplerContext.h"
#include "PCGElement.h"

class UPCGVoxelTerrainData;
class UVoxelLandPCGVoxelSurfaceSamplerSettings;

class FVoxelLandPCGVoxelSurfaceSamplerElement final
	: public IPCGElementWithCustomContext<FVoxelLandPCGVoxelSurfaceSamplerContext>
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual void AbortInternal(FPCGContext* Context) const override;

private:
	static bool Launch(FVoxelLandPCGVoxelSurfaceSamplerContext& Context, const UPCGVoxelTerrainData& Terrain,
		const UVoxelLandPCGVoxelSurfaceSamplerSettings& Settings);
	static void Resume(const TWeakPtr<FPCGContextHandle>& Handle);
};
