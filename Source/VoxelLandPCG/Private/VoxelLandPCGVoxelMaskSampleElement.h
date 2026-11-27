#pragma once

#include "PCGElement.h"

class UPCGBasePointData;
class UPCGVoxelTerrainData;
class UVoxelLandPCGVoxelMaskSampleSettings;
struct FPCGTaggedData;

class FVoxelLandPCGVoxelMaskSampleElement final : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
	static bool SampleInput(FPCGContext& Context, const FPCGTaggedData& Tagged, const UPCGBasePointData& Input,
		const UPCGVoxelTerrainData& Data, const UVoxelLandPCGVoxelMaskSampleSettings& Settings);
};
