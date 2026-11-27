#pragma once

#include "PCGElement.h"

class FVoxelLandPCGVoxelSurfaceSamplerElement final : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
